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
            }
        };
        link = std::make_shared<Link>(std::make_shared<LoopbackSocket>(escs, tick), std::make_shared<SocketNull>(), [](){});
        bus = std::make_unique<Bus>(link);
        bus->configureWaitLatency(0ns, 0ns);
        seq = std::make_unique<ENI::Sequencer>(*bus, link, config);
        return *seq;
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
};


TEST_F(ENISequencer, reaches_safe_op_from_the_eni)
{
    sequencer().requestState(State::SAFE_OP);
    ASSERT_EQ(State::SAFE_OP, sequencer().state());
    for (auto& drive : drives)
    {
        ASSERT_EQ(State::SAFE_OP, drive->sl.state());
    }
    ASSERT_EQ(1001, bus->slaves()[0].address);
    ASSERT_EQ(1002, bus->slaves()[1].address);

    auto [obj, assign] = CoE::findObject(drives[0]->dev.dictionary, 0x1C12, 1);
    EXPECT_EQ(0x1601, *static_cast<uint16_t*>(assign->data));
}

TEST_F(ENISequencer, goes_back_to_init_and_up_again)
{
    sequencer().requestState(State::SAFE_OP);
    sequencer().requestState(State::INIT);
    ASSERT_EQ(State::INIT, sequencer().state());
    for (auto& drive : drives)
    {
        ASSERT_EQ(State::INIT, drive->sl.state());
    }

    std::size_t handlers = bus->slaves()[0].mailbox.to_process.size();
    sequencer().requestState(State::SAFE_OP);
    ASSERT_EQ(State::SAFE_OP, sequencer().state());
    ASSERT_EQ(handlers, bus->slaves()[0].mailbox.to_process.size());
}

TEST_F(ENISequencer, pairs_slaves_by_auto_increment_address)
{
    std::swap(config.slaves[0], config.slaves[1]);
    sequencer().requestState(State::SAFE_OP);
    ASSERT_EQ(1001, bus->slaves()[0].address);
    ASSERT_EQ(1002, bus->slaves()[1].address);
    ASSERT_EQ("Drive 1 (EVS-NET-01)", sequencer().slaveAt(0).info.name);
}

TEST_F(ENISequencer, refuses_inconsistent_configurations)
{
    Bus unused(std::make_shared<Link>(std::make_shared<SocketNull>(), std::make_shared<SocketNull>(), [](){}));

    ENI::Config duplicated = config;
    duplicated.slaves[1].info.auto_inc_addr = 0;
    ASSERT_THROW(ENI::Sequencer(unused, nullptr, duplicated), std::invalid_argument);

    ENI::Sequencer valid(unused, nullptr, config);
    ASSERT_THROW(valid.requestState(State::BOOT), std::invalid_argument);
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
