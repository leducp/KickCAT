// FoE end to end: a real master Bus transfers files with an emulated slave built from an ESI
// and served by a DirectoryStorage, over the in-process loopback.
#include <gtest/gtest.h>

#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "mocks/Time.h"

#include "kickcat/Bus.h"
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
            for (auto const& entry : filesystem::list(DIR))
            {
                filesystem::removeFile(filesystem::join(DIR, entry.name));
            }
            filesystem::removeDirectory(DIR);
        }

        // The ESI fixture sits beside the binary, as does the config.
        void start(std::string const& extra_params = "")
        {
            filesystem::writeFile(CONFIG, std::string{"{\"esi\": \"ecat402-drive.xml\", \"foe_dir\": \""} + DIR + "\""
                                          + extra_params + "}");

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
