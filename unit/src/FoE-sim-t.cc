// FoE end to end: a real master Bus transfers files with an emulated slave built from an ESI
// and served by a DirectoryStorage, over the in-process loopback.
#include <gtest/gtest.h>

#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/CoE/mailbox/request.h"
#include "kickcat/FoE/protocol.h"
#include "kickcat/Link.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/OS/Filesystem.h"
#include "kickcat/SocketNull.h"
#include "kickcat/simulation/SimulatedSlave.h"

using namespace kickcat;

namespace
{
    constexpr char const* CONFIG = "foe_sim_test.json";
    constexpr char const* DIR    = "foe_sim_test_dir";
    constexpr char const* ESI    = "ecat402-drive.xml";
    constexpr char const* ASYMMETRIC_ESI = "foe_sim_test_asymmetric.xml";

    class FoESimTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            resetMockClock();
            filesystem::createDirectory(DIR);
        }

        void TearDown() override
        {
            resetMockClock();
            bus_.reset();
            link_.reset();
            loopback_.reset();
            sim_.reset();
            filesystem::removeFile(CONFIG);
            filesystem::removeFile(ASYMMETRIC_ESI);
            for (auto const& entry : filesystem::list(DIR))
            {
                filesystem::removeFile(filesystem::join(DIR, entry.name));
            }
            filesystem::removeDirectory(DIR);
        }

        // The ESI fixture sits beside the binary, as does the config.
        void start(std::string const& extra_params = "", std::string const& esi = ESI)
        {
            filesystem::writeFile(CONFIG, "{\"esi\": \"" + esi + "\", \"foe_dir\": \"" + DIR + "\"" + extra_params + "}");

            sim_ = std::make_unique<sim::SimulatedSlave>(sim::buildSlave(CONFIG));
            sim_->slave->start();

            loopback_ = std::make_shared<LoopbackSocket>(std::vector<EmulatedESC*>{sim_->esc.get()},
                                                         [this]() { sim_->slave->routine(); });
            link_ = std::make_shared<Link>(loopback_, std::make_shared<SocketNull>(), [](){});
            bus_  = std::make_unique<Bus>(link_);
            bus_->init(100ms);
            ASSERT_EQ(1u, bus_->slaves().size());
        }

        Slave& slave()
        {
            return bus_->slaves().at(0);
        }

        std::unique_ptr<sim::SimulatedSlave> sim_;
        std::shared_ptr<LoopbackSocket> loopback_;
        std::shared_ptr<Link> link_;
        std::unique_ptr<Bus> bus_;
    };
}

TEST_F(FoESimTest, write_then_read)
{
    start();

    // Several packets, with a partial last one: kept small since every now() burns 1ms of mock time
    std::vector<uint8_t> file(700);
    std::iota(file.begin(), file.end(), uint8_t{3});

    bus_->writeFoE(slave(), "fw.bin", 0, file);
    ASSERT_EQ(file, filesystem::readFile(filesystem::join(DIR, "fw.bin")));

    std::vector<uint8_t> read_back;
    bus_->readFoE(slave(), "fw.bin", 0, read_back);
    ASSERT_EQ(file, read_back);

    // CoE is still served next to FoE
    uint32_t vendor_id = 0;
    uint32_t size = sizeof(vendor_id);
    bus_->readSDO(slave(), 0x1018, 1, Bus::Access::PARTIAL, &vendor_id, &size);
    ASSERT_EQ(slave().sii.info.vendor_id, vendor_id);
}

TEST_F(FoESimTest, not_found)
{
    start();

    std::vector<uint8_t> file;
    try
    {
        bus_->readFoE(slave(), "missing.bin", 0, file);
        FAIL() << "readFoE shall throw";
    }
    catch (ErrorFoE const& e)
    {
        ASSERT_EQ(FoE::result::NOT_FOUND, e.code());
    }
}

TEST_F(FoESimTest, password)
{
    start(", \"foe_password\": 4660");

    try
    {
        bus_->writeFoE(slave(), "fw.bin", 0x1111, {1, 2, 3});
        FAIL() << "writeFoE shall throw";
    }
    catch (ErrorFoE const& e)
    {
        ASSERT_EQ(FoE::result::NO_RIGHTS, e.code());
    }

    bus_->writeFoE(slave(), "fw.bin", 0x1234, {1, 2, 3});
    ASSERT_EQ((std::vector<uint8_t>{1, 2, 3}), filesystem::readFile(filesystem::join(DIR, "fw.bin")));
}

TEST_F(FoESimTest, missing_directory)
{
    filesystem::writeFile(CONFIG, "{\"esi\": \"ecat402-drive.xml\", \"foe_dir\": \"no_such_dir\"}");
    ASSERT_THROW(sim::buildSlave(CONFIG), std::runtime_error);
}


class FoESimAsymmetricTest : public FoESimTest, public testing::WithParamInterface<std::tuple<int, int>>
{
};

TEST_P(FoESimAsymmetricTest, mailbox_sizes)
{
    auto [receive, send] = GetParam();

    // Fixture: 128-byte receive/send mailboxes at 0x1000/0x1400.
    std::vector<uint8_t> raw = filesystem::readFile(ESI);
    std::string esi{raw.begin(), raw.end()};
    auto resize = [&](std::string const& address, int size)
    {
        std::string const from = "DefaultSize=\"128\" StartAddress=\"" + address + "\"";
        std::size_t pos = esi.find(from);
        ASSERT_NE(std::string::npos, pos);
        esi.replace(pos, from.size(), "DefaultSize=\"" + std::to_string(size) + "\" StartAddress=\"" + address + "\"");
    };
    resize("#x1000", receive);
    resize("#x1400", send);
    filesystem::writeFile(ASYMMETRIC_ESI, esi);

    start("", ASYMMETRIC_ESI);
    ASSERT_EQ(receive, slave().mailbox.recv_size);
    ASSERT_EQ(send, slave().mailbox.send_size);

    std::vector<uint8_t> file(700);
    std::iota(file.begin(), file.end(), uint8_t{5});
    bus_->writeFoE(slave(), "fw.bin", 0, file);
    std::vector<uint8_t> read_back;
    bus_->readFoE(slave(), "fw.bin", 0, read_back);
    ASSERT_EQ(file, read_back);

    uint32_t vendor_id = 0;
    uint32_t size = sizeof(vendor_id);
    bus_->readSDO(slave(), 0x1018, 1, Bus::Access::PARTIAL, &vendor_id, &size);
    ASSERT_EQ(slave().sii.info.vendor_id, vendor_id);

    std::vector<uint8_t> list(4096);
    uint32_t list_size = static_cast<uint32_t>(list.size());
    auto info = slave().mailbox.createSDOInfoGetODList(CoE::SDO::information::ListType::ALL, list.data(), &list_size, 1s);
    bus_->waitForMessage(info);
    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, info->status());
    ASSERT_GT(list_size, static_cast<uint32_t>(send));
    ASSERT_EQ(sizeof(uint16_t) * (1 + sim_->dictionary->size()), list_size);
}

INSTANTIATE_TEST_SUITE_P(Sizes, FoESimAsymmetricTest, testing::Values(std::make_tuple(200, 96), std::make_tuple(96, 200)));
