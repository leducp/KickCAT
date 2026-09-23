#include <algorithm>

#include <gtest/gtest.h>

#include "kickcat/Error.h"
#include "kickcat/OS/Filesystem.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace kickcat;
using namespace kickcat::filesystem;

TEST(FilesystemPath, parent_keeps_the_root_and_yields_nothing_for_a_bare_name)
{
    EXPECT_EQ("", parent("config.json"));
    EXPECT_EQ("dir", parent("dir/config.json"));
    EXPECT_EQ("a/b", parent("a/b/c"));
    EXPECT_EQ("/", parent("/config.json"));
    EXPECT_EQ("/a", parent("/a/b"));
    EXPECT_EQ("", parent(""));
}

TEST(FilesystemPath, filename_is_everything_after_the_last_separator)
{
    EXPECT_EQ("config.json", filename("config.json"));
    EXPECT_EQ("config.json", filename("dir/config.json"));
    EXPECT_EQ("config.json", filename("/a/b/config.json"));
    EXPECT_EQ("", filename("dir/"));
    EXPECT_EQ("", filename(""));
}

TEST(FilesystemPath, extension_ignores_a_leading_dot_and_directories)
{
    EXPECT_EQ(".xml", extension("device.xml"));
    EXPECT_EQ(".xml", extension("a/b/device.xml"));
    EXPECT_EQ(".gz", extension("archive.tar.gz"));
    EXPECT_EQ("", extension("Makefile"));
    // A dotfile has no extension, and a dot in a parent must not be mistaken for one.
    EXPECT_EQ("", extension(".gitignore"));
    EXPECT_EQ("", extension("a.b/Makefile"));
}

TEST(FilesystemPath, join_inserts_one_separator_and_only_where_needed)
{
    EXPECT_EQ("dir/name", join("dir", "name"));
    EXPECT_EQ("dir/name", join("dir/", "name"));
    EXPECT_EQ("/name", join("/", "name"));
    // parent() of a bare filename is empty: joining onto it must not produce a rooted path.
    EXPECT_EQ("name", join("", "name"));
}

class FilesystemTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // Beside the test binary, like the ESI fixtures.
        root_ = "kickcat_filesystem-t.d";
        cleanup();
        ASSERT_TRUE(createDirectory(root_));
    }

    void TearDown() override
    {
        cleanup();
    }

    void cleanup()
    {
        if (not isDirectory(root_))
        {
            removeFile(root_);
            return;
        }
        for (auto const& file : listFilesRecursive(root_))
        {
            removeFile(file);
        }
        // Deepest first: removeDirectory only takes empty ones.
        std::vector<std::string> directories;
        collectDirectories(root_, directories);
        std::sort(directories.begin(), directories.end(),
            [](std::string const& a, std::string const& b) { return a.size() > b.size(); });
        for (auto const& directory : directories)
        {
            removeDirectory(directory);
        }
        removeDirectory(root_);
    }

    void collectDirectories(std::string const& directory, std::vector<std::string>& out)
    {
        for (auto const& entry : list(directory))
        {
            if (entry.is_directory)
            {
                std::string path = join(directory, entry.name);
                out.push_back(path);
                collectDirectories(path, out);
            }
        }
    }

    std::string root_;
};

TEST_F(FilesystemTest, write_then_read_returns_the_same_bytes)
{
    std::string path = join(root_, "payload.bin");
    std::vector<uint8_t> written{0x00, 0x01, 0xFF, 0x7F, 0x00, 0x42};
    writeFile(path, written.data(), written.size());

    EXPECT_TRUE(exists(path));
    EXPECT_FALSE(isDirectory(path));
    EXPECT_EQ(written, readFile(path));
}

TEST_F(FilesystemTest, write_truncates_an_existing_file)
{
    std::string path = join(root_, "payload.bin");
    writeFile(path, std::string{"a long first content"});
    writeFile(path, std::string{"short"});

    std::vector<uint8_t> content = readFile(path);
    EXPECT_EQ(std::string(content.begin(), content.end()), "short");
}

TEST_F(FilesystemTest, an_empty_file_reads_back_empty)
{
    std::string path = join(root_, "empty.bin");
    writeFile(path, nullptr, 0);

    EXPECT_TRUE(exists(path));
    EXPECT_TRUE(readFile(path).empty());
}

TEST_F(FilesystemTest, a_payload_bigger_than_one_read_chunk_survives)
{
    // The backends read and write in 64k chunks: cross that boundary with a non-repeating pattern.
    std::vector<uint8_t> written(200000);
    for (std::size_t i = 0; i < written.size(); ++i)
    {
        written[i] = static_cast<uint8_t>(i * 7);
    }

    std::string path = join(root_, "big.bin");
    writeFile(path, written.data(), written.size());
    EXPECT_EQ(written, readFile(path));
}

TEST_F(FilesystemTest, reading_an_absent_file_throws)
{
    EXPECT_THROW(readFile(join(root_, "absent.bin")), std::system_error);
}

TEST_F(FilesystemTest, absent_paths_are_reported_absent)
{
    std::string path = join(root_, "absent.bin");
    EXPECT_FALSE(exists(path));
    EXPECT_FALSE(isDirectory(path));
    EXPECT_FALSE(isDirectory(join(root_, "absent.d")));
}

TEST_F(FilesystemTest, removing_a_file_reports_whether_it_was_there)
{
    std::string path = join(root_, "payload.bin");
    writeFile(path, std::string{"content"});

    EXPECT_TRUE(removeFile(path));
    EXPECT_FALSE(exists(path));
    EXPECT_FALSE(removeFile(path));
}

TEST_F(FilesystemTest, creating_and_removing_a_directory_report_whether_it_was_there)
{
    std::string path = join(root_, "nested");

    EXPECT_TRUE(createDirectory(path));
    EXPECT_TRUE(isDirectory(path));
    EXPECT_FALSE(createDirectory(path));

    EXPECT_TRUE(removeDirectory(path));
    EXPECT_FALSE(exists(path));
    EXPECT_FALSE(removeDirectory(path));
}

TEST_F(FilesystemTest, list_reports_one_level_with_its_kinds_and_no_dot_entries)
{
    writeFile(join(root_, "a.xml"), std::string{"a"});
    writeFile(join(root_, "b.bin"), std::string{"b"});
    ASSERT_TRUE(createDirectory(join(root_, "sub")));
    writeFile(join(root_, "sub/deep.xml"), std::string{"deep"});

    std::vector<Entry> entries = list(root_);
    ASSERT_EQ(3u, entries.size()) << "'.' and '..' must not be listed, and 'sub' is not descended";

    std::sort(entries.begin(), entries.end(),
        [](Entry const& a, Entry const& b) { return a.name < b.name; });
    EXPECT_EQ("a.xml", entries[0].name);
    EXPECT_FALSE(entries[0].is_directory);
    EXPECT_EQ("b.bin", entries[1].name);
    EXPECT_FALSE(entries[1].is_directory);
    EXPECT_EQ("sub", entries[2].name);
    EXPECT_TRUE(entries[2].is_directory);
}

TEST_F(FilesystemTest, list_of_an_absent_directory_throws)
{
    EXPECT_THROW(list(join(root_, "absent.d")), std::system_error);
}

TEST_F(FilesystemTest, list_of_an_empty_directory_is_empty)
{
    EXPECT_TRUE(list(root_).empty());
}

TEST_F(FilesystemTest, recursive_list_returns_files_only_as_full_paths)
{
    writeFile(join(root_, "top.xml"), std::string{"top"});
    ASSERT_TRUE(createDirectory(join(root_, "sub")));
    ASSERT_TRUE(createDirectory(join(root_, "sub/deeper")));
    writeFile(join(root_, "sub/middle.xml"), std::string{"middle"});
    writeFile(join(root_, "sub/deeper/bottom.xml"), std::string{"bottom"});

    std::vector<std::string> files = listFilesRecursive(root_);
    std::sort(files.begin(), files.end());

    ASSERT_EQ(3u, files.size());
    EXPECT_EQ(join(root_, "sub/deeper/bottom.xml"), files[0]);
    EXPECT_EQ(join(root_, "sub/middle.xml"), files[1]);
    EXPECT_EQ(join(root_, "top.xml"), files[2]);
}

TEST_F(FilesystemTest, regular_file_read_and_write)
{
    std::string path = join(root_, "payload.bin");
    writeFile(path, std::string{"a long first content"});

    std::string content{"short"};
    writeRegularFile(path, content.data(), content.size());
    std::vector<uint8_t> read = readRegularFile(path);
    EXPECT_EQ(content, std::string(read.begin(), read.end()));

    std::string created = join(root_, "created.bin");
    writeRegularFile(created, content.data(), content.size());
    EXPECT_EQ(read, readFile(created));
}

TEST_F(FilesystemTest, regular_file_read_of_an_absent_file_throws)
{
    EXPECT_THROW(readRegularFile(join(root_, "absent.bin")), std::system_error);
}

#ifndef _WIN32
TEST_F(FilesystemTest, regular_file_does_not_follow_a_symbolic_link)
{
    std::string target = join(root_, "target.bin");
    std::string link   = join(root_, "link.bin");
    writeFile(target, std::string{"secret"});
    ASSERT_EQ(0, ::symlink("target.bin", link.c_str()));

    EXPECT_THROW(readRegularFile(link), std::system_error);

    // The write replaces the link itself
    std::string content{"overwritten"};
    writeRegularFile(link, content.data(), content.size());
    std::vector<uint8_t> kept = readFile(target);
    EXPECT_EQ("secret", std::string(kept.begin(), kept.end()));
    std::vector<uint8_t> written = readRegularFile(link);
    EXPECT_EQ(content, std::string(written.begin(), written.end()));
}

TEST_F(FilesystemTest, regular_file_does_not_go_through_a_hard_link)
{
    std::string target = join(root_, "target.bin");
    std::string link   = join(root_, "link.bin");
    writeFile(target, std::string{"secret"});
    ASSERT_EQ(0, ::link(target.c_str(), link.c_str()));

    EXPECT_THROW(readRegularFile(link), std::system_error);

    // The write gives this name its own file: the other name keeps the previous content
    std::string content{"overwritten"};
    writeRegularFile(link, content.data(), content.size());
    std::vector<uint8_t> kept = readFile(target);
    EXPECT_EQ("secret", std::string(kept.begin(), kept.end()));
    std::vector<uint8_t> written = readRegularFile(link);
    EXPECT_EQ(content, std::string(written.begin(), written.end()));
}

TEST_F(FilesystemTest, regular_file_write_keeps_the_permissions)
{
    std::string content{"data"};

    std::string restricted = join(root_, "restricted.bin");
    writeFile(restricted, std::string{"old"});
    ASSERT_EQ(0, ::chmod(restricted.c_str(), 0600));
    writeRegularFile(restricted, content.data(), content.size());
    struct stat info;
    ASSERT_EQ(0, ::stat(restricted.c_str(), &info));
    EXPECT_EQ(0600u, info.st_mode & 07777);

    // Never a set-user-ID file with a new content
    std::string setuid = join(root_, "setuid.bin");
    writeFile(setuid, std::string{"old"});
    ASSERT_EQ(0, ::chmod(setuid.c_str(), 04755));
    writeRegularFile(setuid, content.data(), content.size());
    ASSERT_EQ(0, ::stat(setuid.c_str(), &info));
    EXPECT_EQ(0755u, info.st_mode & 07777);

    mode_t mask = ::umask(0);
    ::umask(mask);
    std::string created = join(root_, "created.bin");
    writeRegularFile(created, content.data(), content.size());
    ASSERT_EQ(0, ::stat(created.c_str(), &info));
    EXPECT_EQ(0644u & ~mask, info.st_mode & 07777);
}

TEST_F(FilesystemTest, regular_file_refuses_a_fifo)
{
    std::string fifo = join(root_, "fifo");
    ASSERT_EQ(0, ::mkfifo(fifo.c_str(), 0600));

    EXPECT_THROW(readRegularFile(fifo), std::system_error);
}

TEST_F(FilesystemTest, regular_file_refuses_a_directory)
{
    std::string directory = join(root_, "directory");
    ASSERT_TRUE(createDirectory(directory));

    EXPECT_THROW(readRegularFile(directory), std::system_error);

    // A failed write leaves nothing behind
    std::string content{"data"};
    EXPECT_THROW(writeRegularFile(directory, content.data(), content.size()), std::system_error);
    EXPECT_EQ(1u, list(root_).size());
}
#endif
