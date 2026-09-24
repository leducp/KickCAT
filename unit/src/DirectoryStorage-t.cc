#include <gtest/gtest.h>

#include <numeric>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "kickcat/FoE/protocol.h"
#include "kickcat/OS/Filesystem.h"
#include "kickcat/simulation/DirectoryStorage.h"

using namespace kickcat;
using kickcat::sim::DirectoryStorage;

class DirectoryStorageTest : public testing::Test
{
protected:
    void SetUp() override
    {
        filesystem::createDirectory(DIR);
        config_.directory = DIR;
    }

    void TearDown() override
    {
        for (auto const& entry : filesystem::list(DIR))
        {
            filesystem::removeFile(filesystem::join(DIR, entry.name));
        }
        filesystem::removeDirectory(DIR);
    }

    std::vector<uint8_t> readAll(FoE::AbstractReader& reader, uint32_t chunk)
    {
        std::vector<uint8_t> content;
        std::vector<uint8_t> buffer(chunk);
        uint32_t size = chunk;
        while (size == chunk)
        {
            EXPECT_EQ(0, reader.read(buffer.data(), chunk, size));
            content.insert(content.end(), buffer.begin(), buffer.begin() + size);
        }
        return content;
    }

    uint32_t writeFile(DirectoryStorage& storage, std::string_view name, std::vector<uint8_t> const& content)
    {
        auto [rc, writer] = storage.openWrite(name, 0);
        if (rc != 0)
        {
            return rc;
        }
        rc = writer->write(content.data(), static_cast<uint32_t>(content.size()));
        if (rc != 0)
        {
            return rc;
        }
        return writer->commit();
    }

    static constexpr char const* DIR = "foe_storage_test_dir";
    DirectoryStorage::Config config_;
};

TEST_F(DirectoryStorageTest, write_then_read)
{
    std::vector<uint8_t> file(100);
    std::iota(file.begin(), file.end(), uint8_t{0});

    DirectoryStorage storage{config_};
    {
        auto [rc, writer] = storage.openWrite("fw.bin", 0);
        ASSERT_EQ(0, rc);
        ASSERT_EQ(0, writer->write(file.data(), 60));
        ASSERT_EQ(0, writer->write(file.data() + 60, 40));
        ASSERT_EQ(0, writer->commit());
    }
    ASSERT_EQ(file, filesystem::readFile(filesystem::join(DIR, "fw.bin")));

    auto [rc, reader] = storage.openRead("fw.bin", 0);
    ASSERT_EQ(0, rc);
    ASSERT_EQ(file, readAll(*reader, 32));
}

TEST_F(DirectoryStorageTest, uncommitted_write_leaves_no_file)
{
    uint8_t data[4] = {1, 2, 3, 4};
    DirectoryStorage storage{config_};
    {
        auto [rc, writer] = storage.openWrite("fw.bin", 0);
        ASSERT_EQ(0, rc);
        ASSERT_EQ(0, writer->write(data, sizeof(data)));
    }
    ASSERT_FALSE(filesystem::exists(filesystem::join(DIR, "fw.bin")));
}

TEST_F(DirectoryStorageTest, not_found)
{
    ASSERT_TRUE(filesystem::createDirectory(filesystem::join(DIR, "sub")));

    DirectoryStorage storage{config_};
    ASSERT_EQ(FoE::result::NOT_FOUND, std::get<0>(storage.openRead("missing", 0)));
    ASSERT_EQ(FoE::result::NOT_FOUND, std::get<0>(storage.openRead("sub", 0)));
    filesystem::removeDirectory(filesystem::join(DIR, "sub"));
}

TEST_F(DirectoryStorageTest, names_outside_directory_are_rejected)
{
    DirectoryStorage storage{config_};
    for (std::string_view name : {"", ".", "..", "../fw.bin", "sub/fw.bin", "sub\\fw.bin", "C:fw.bin", "fw\n.bin"})
    {
        EXPECT_EQ(FoE::result::ACCESS_DENIED, std::get<0>(storage.openWrite(name, 0))) << name;
        EXPECT_EQ(FoE::result::ACCESS_DENIED, std::get<0>(storage.openRead(name, 0))) << name;
    }
}

TEST_F(DirectoryStorageTest, password)
{
    config_.password = 0x1234;
    DirectoryStorage storage{config_};
    ASSERT_EQ(FoE::result::NO_RIGHTS, std::get<0>(storage.openWrite("fw.bin", 0)));
    ASSERT_EQ(FoE::result::NO_RIGHTS, std::get<0>(storage.openWrite("fw.bin", 0x4321)));
    ASSERT_EQ(0, std::get<0>(storage.openWrite("fw.bin", 0x1234)));
}

TEST_F(DirectoryStorageTest, read_only)
{
    config_.read_only = true;
    DirectoryStorage storage{config_};
    ASSERT_EQ(FoE::result::ACCESS_DENIED, std::get<0>(storage.openWrite("fw.bin", 0)));
}

TEST_F(DirectoryStorageTest, max_size)
{
    config_.max_size = 8;
    uint8_t data[6] = {};
    DirectoryStorage storage{config_};
    auto [rc, writer] = storage.openWrite("fw.bin", 0);
    ASSERT_EQ(0, rc);
    ASSERT_EQ(0, writer->write(data, sizeof(data)));
    ASSERT_EQ(FoE::result::DISK_FULL, writer->write(data, sizeof(data)));
}

TEST_F(DirectoryStorageTest, concurrent_transfers)
{
    DirectoryStorage storage{config_};
    ASSERT_EQ(0, writeFile(storage, "a.bin", {1, 2, 3}));

    auto [rc_write, writer] = storage.openWrite("b.bin", 0);
    ASSERT_EQ(0, rc_write);
    auto [rc_read, reader] = storage.openRead("a.bin", 0);
    ASSERT_EQ(0, rc_read);

    uint8_t data[2] = {4, 5};
    ASSERT_EQ(0, writer->write(data, sizeof(data)));
    ASSERT_EQ((std::vector<uint8_t>{1, 2, 3}), readAll(*reader, 8));
    ASSERT_EQ(0, writer->commit());
    ASSERT_EQ((std::vector<uint8_t>{4, 5}), filesystem::readFile(filesystem::join(DIR, "b.bin")));
}

#ifndef _WIN32
TEST_F(DirectoryStorageTest, links_never_reach_outside_files)
{
    constexpr char const* OUTSIDE = "foe_storage_test_outside.bin";
    filesystem::writeFile(OUTSIDE, std::string{"secret"});
    ASSERT_EQ(0, ::symlink((std::string{"../"} + OUTSIDE).c_str(), filesystem::join(DIR, "symlink.bin").c_str()));
    ASSERT_EQ(0, ::link(OUTSIDE, filesystem::join(DIR, "hardlink.bin").c_str()));

    DirectoryStorage storage{config_};
    for (std::string_view name : {"symlink.bin", "hardlink.bin"})
    {
        EXPECT_EQ(FoE::result::ACCESS_DENIED, std::get<0>(storage.openRead(name, 0))) << name;

        // A write replaces the link by a regular file of the directory
        EXPECT_EQ(0, writeFile(storage, name, {1, 2, 3, 4})) << name;
        EXPECT_EQ(0, std::get<0>(storage.openRead(name, 0))) << name;
    }

    std::vector<uint8_t> kept = filesystem::readFile(OUTSIDE);
    EXPECT_EQ("secret", std::string(kept.begin(), kept.end()));
    filesystem::removeFile(OUTSIDE);
}
#endif
