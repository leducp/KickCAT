#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/ENI/Parser.h"
#include "kickcat/ENI/Sequencer.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/Link.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/PDO.h"
#include "kickcat/SocketNull.h"
#include "kickcat/slave/Slave.h"

using namespace kickcat;

namespace
{
    constexpr uint32_t SLAVE_PD_SIZE = 256;

    struct Drive
    {
        Drive(ESI::Device device)
            : dev(std::move(device))
            , pdo(&esc)
            , sl(&esc, &pdo)
            , inputs(SLAVE_PD_SIZE, 0)
            , outputs(SLAVE_PD_SIZE, 0)
        {
            CoE::materializeStorage(dev.dictionary);
            esc.loadEeprom(ESI::buildEepromImage(dev));
            mbx = std::make_unique<mailbox::response::Mailbox>(&esc, 1024);
            mbx->enableCoE(dev.dictionary);
            sl.setMailbox(mbx.get());
            sl.setDictionary(&dev.dictionary);
            pdo.setInput(inputs.data(), SLAVE_PD_SIZE);
            pdo.setOutput(outputs.data(), SLAVE_PD_SIZE);
            sl.start();
        }

        ESI::Device dev;
        EmulatedESC esc;
        PDO pdo;
        slave::Slave sl;
        std::unique_ptr<mailbox::response::Mailbox> mbx;
        std::vector<uint8_t> inputs;
        std::vector<uint8_t> outputs;
    };
}

class ENISequencer : public testing::Test
{
public:
    void SetUp() override
    {
        resetMockClock();
        config = ENI::loadFile("kickcat_eni_test_drives.xml");
        for (int i = 0; i < 2; ++i)
        {
            ESI::Parser parser;
            drives.push_back(std::make_unique<Drive>(parser.loadDevice("ecat402-drive.xml", {})));
        }
    }

    // Built on demand so that each test can edit the configuration first
    ENI::Sequencer& sequencer()
    {
        if (seq)
        {
            return *seq;
        }
        std::vector<EmulatedESC*> escs;
        for (auto& drive : drives)
        {
            escs.push_back(&drive->esc);
        }
        auto tick = [this]()
        {
            for (auto& drive : drives)
            {
                drive->sl.routine();
                if (outputs_valid and drive->sl.state() == State::SAFE_OP)
                {
                    drive->sl.validateOutputData();
                }
            }
        };
        link = std::make_shared<Link>(std::make_shared<LoopbackSocket>(escs, tick), std::make_shared<SocketNull>(), [](){});
        bus = std::make_unique<Bus>(link);
        bus->configureWaitLatency(0ns, 0ns);
        seq = std::make_unique<ENI::Sequencer>(*bus, link, config);
        return *seq;
    }

    void cycle()
    {
        bus->processDataReadWrite([this](DatagramState const&) { ++cyclic_errors; });
    }

    void goToOperational()
    {
        sequencer().requestState(State::SAFE_OP);
        iomap.assign(sequencer().processImageSize(), 0);
        sequencer().mapProcessImage(iomap.data(), iomap.size());
        cycle();
        outputs_valid = true;
        sequencer().requestState(State::OPERATIONAL, [this]() { cycle(); });
    }

    void removeInitCmds(ENI::Slave& slave, char const* comment)
    {
        std::vector<ENI::InitCmd> kept;
        for (auto& cmd : slave.init_cmds)
        {
            if (cmd.comment.find(comment) == std::string::npos)
            {
                kept.push_back(cmd);
            }
        }
        slave.init_cmds = kept;
    }

    void expectThrowContaining(std::function<void()> const& action, char const* fragment)
    {
        try
        {
            action();
            FAIL() << "expected an exception containing '" << fragment << "'";
        }
        catch (std::exception const& e)
        {
            EXPECT_NE(std::string{e.what()}.find(fragment), std::string::npos) << e.what();
        }
    }

    ENI::Config config;
    std::vector<std::unique_ptr<Drive>> drives;
    std::shared_ptr<Link> link;
    std::unique_ptr<Bus> bus;
    std::unique_ptr<ENI::Sequencer> seq;
    std::vector<uint8_t> iomap;
    bool outputs_valid = false;
    int cyclic_errors = 0;
};


TEST_F(ENISequencer, reaches_operational_and_exchanges_process_data)
{
    goToOperational();
    ASSERT_EQ(State::OPERATIONAL, sequencer().state());
    for (auto& drive : drives)
    {
        ASSERT_EQ(State::OPERATIONAL, drive->sl.state());
    }

    // Layout programmed by the ENI: outputs then inputs, RxPDO 0x1601 (6 bytes), TxPDO 0x1A00 (11 bytes)
    ASSERT_EQ(2 * (6 + 11), iomap.size());
    std::vector<Slave>& slaves = bus->slaves();
    ASSERT_EQ(0x10000u, slaves[0].output.address);
    ASSERT_EQ(0x10006u, slaves[1].output.address);
    ASSERT_EQ(0x1000Cu, slaves[0].input.address);
    ASSERT_EQ(0x10017u, slaves[1].input.address);
    ASSERT_EQ(1001, slaves[0].address);
    ASSERT_EQ(1002, slaves[1].address);

    for (std::size_t i = 0; i < drives.size(); ++i)
    {
        uint16_t status = static_cast<uint16_t>(0x1230 + i);
        std::memcpy(drives[i]->inputs.data(), &status, sizeof(status));
        uint16_t control = static_cast<uint16_t>(0x0F00 + i);
        std::memcpy(slaves[i].output.data, &control, sizeof(control));
    }
    for (int i = 0; i < 5; ++i)
    {
        cycle();
    }
    for (std::size_t i = 0; i < drives.size(); ++i)
    {
        uint16_t status = 0;
        std::memcpy(&status, slaves[i].input.data, sizeof(status));
        uint16_t control = 0;
        std::memcpy(&control, drives[i]->outputs.data(), sizeof(control));
        EXPECT_EQ(0x1230 + i, status);
        EXPECT_EQ(0x0F00 + i, control);
    }
    EXPECT_EQ(0, cyclic_errors);

    auto [obj, assign] = CoE::findObject(drives[0]->dev.dictionary, 0x1C12, 1);
    EXPECT_EQ(0x1601, *static_cast<uint16_t*>(assign->data));
}

TEST_F(ENISequencer, goes_back_to_init_and_up_again)
{
    goToOperational();
    sequencer().requestState(State::INIT);
    ASSERT_EQ(State::INIT, sequencer().state());
    for (auto& drive : drives)
    {
        ASSERT_EQ(State::INIT, drive->sl.state());
    }
    ASSERT_THROW(sequencer().processImageSize(), std::logic_error);

    std::size_t handlers = bus->slaves()[0].mailbox.to_process.size();
    outputs_valid = false;
    goToOperational();
    ASSERT_EQ(State::OPERATIONAL, sequencer().state());
    ASSERT_EQ(handlers, bus->slaves()[0].mailbox.to_process.size());
}

TEST_F(ENISequencer, pairs_slaves_by_auto_increment_address)
{
    std::swap(config.slaves[0], config.slaves[1]);
    goToOperational();
    ASSERT_EQ(1001, bus->slaves()[0].address);
    ASSERT_EQ(1002, bus->slaves()[1].address);
    ASSERT_EQ("Drive 1 (EVS-NET-01)", sequencer().slaveAt(0).info.name);
    ASSERT_EQ(0x10000u, bus->slaves()[0].output.address);
}

TEST_F(ENISequencer, refuses_inconsistent_configurations)
{
    Bus unused(std::make_shared<Link>(std::make_shared<SocketNull>(), std::make_shared<SocketNull>(), [](){}));

    ENI::Config duplicated = config;
    duplicated.slaves[1].info.auto_inc_addr = 0;
    ASSERT_THROW(ENI::Sequencer(unused, nullptr, duplicated), std::invalid_argument);

    ENI::Config dc = config;
    for (auto& slave : dc.slaves)
    {
        slave.dc = ENI::Dc{};
        slave.dc->cycle_time0 = 1ms;
        slave.dc->shift_time = 250us;
    }
    ENI::Sequencer with_dc(unused, nullptr, dc);
    EXPECT_EQ(1ms, with_dc.cycleTime());
    EXPECT_EQ(250us, with_dc.shiftTime());

    dc.slaves[1].dc->shift_time = 0ns;
    ASSERT_THROW(ENI::Sequencer(unused, nullptr, dc), std::invalid_argument);

    ENI::Sequencer without_dc(unused, nullptr, config);
    EXPECT_FALSE(without_dc.cycleTime().has_value());
    ASSERT_THROW(without_dc.requestState(State::BOOT), std::invalid_argument);
}

TEST_F(ENISequencer, refuses_a_slave_count_mismatch)
{
    config.slaves.pop_back();
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "found 2 slave(s), the ENI describes 1");
}

TEST_F(ENISequencer, refuses_an_identity_differing_from_the_sii)
{
    config.slaves[1].info.revision_no += 1;
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "Drive 2 (EVS-NET-01): identity differs from the SII");
}

TEST_F(ENISequencer, refuses_a_mailbox_protocol_missing_from_the_sii)
{
    config.slaves[0].mailbox->protocols |= ENI::protocol::SoE;
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "mailbox protocols not supported by the SII");
}

TEST_F(ENISequencer, refuses_mailbox_sync_managers_the_eni_did_not_program)
{
    removeInitCmds(config.slaves[0], "mailbox");
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "SM0/SM1 do not hold the mailbox the ENI describes");
}

TEST_F(ENISequencer, checks_explicit_identification)
{
    config.slaves[1].info.identification = ENI::Identification{reg::STATION_ALIAS, 0x0159};
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "explicit identification 0x0 differs from 0x159");
}

TEST_F(ENISequencer, accepts_matching_explicit_identification)
{
    config.slaves[1].info.identification = ENI::Identification{reg::STATION_ALIAS, 0};
    sequencer().requestState(State::PRE_OP);
    ASSERT_EQ(State::PRE_OP, sequencer().state());
}

TEST_F(ENISequencer, refuses_process_data_differing_from_the_eni)
{
    config.slaves[0].process_data.recv[0].bit_length = 80;
    expectThrowContaining([this]() { sequencer().requestState(State::SAFE_OP); }, "programmed process data sizes differ from the ENI ProcessData");
}

TEST_F(ENISequencer, process_data_description_is_optional)
{
    for (auto& slave : config.slaves)
    {
        slave.process_data = {};
    }
    goToOperational();
    ASSERT_EQ(2 * (6 + 11), iomap.size());
}

TEST_F(ENISequencer, reports_a_validate_timeout)
{
    for (auto& cmd : config.slaves[0].init_cmds)
    {
        if (cmd.comment == "check device state for PREOP")
        {
            cmd.validate->data = {0x08, 0x00};
        }
    }
    expectThrowContaining([this]() { sequencer().requestState(State::PRE_OP); }, "'check device state for PREOP' validate timeout");
}

TEST_F(ENISequencer, process_image_needs_safe_op)
{
    sequencer().requestState(State::PRE_OP);
    uint8_t buffer[64];
    ASSERT_THROW(sequencer().mapProcessImage(buffer, sizeof(buffer)), std::logic_error);
}

TEST_F(ENISequencer, coe_sent_after_the_safe_op_request_is_refused_by_the_slave)
{
    // The PDO assignment of drive 1 arrives one transition late: SAFE_OP is requested with the
    // device default RxPDO 0x1600 (11 bytes) on the 6-byte SM2 the ENI programmed.
    for (auto& cmd : config.slaves[0].mailbox->coe_init_cmds)
    {
        if (cmd.transitions & ENI::transition::PS)
        {
            cmd.transitions = ENI::transition::SO;
        }
    }
    expectThrowContaining([this]() { sequencer().requestState(State::SAFE_OP); }, "Drive 1 (EVS-NET-01): 'check device state for SAFEOP' validate timeout");

    ASSERT_EQ(State::PRE_OP, drives[0]->sl.state());
    uint16_t al_status_code = 0;
    drives[0]->esc.read(reg::AL_STATUS_CODE, &al_status_code, sizeof(al_status_code));
    ASSERT_EQ(StatusCode::INVALID_OUTPUT_CONFIGURATION, al_status_code);
}
