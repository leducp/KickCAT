#include <algorithm>

#include <gtest/gtest.h>

#include "kickcat/Error.h"
#include "kickcat/OS/Filesystem.h"

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
