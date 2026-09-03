#include "GameEditor/FileExplorer.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace ya
{

namespace
{

struct FScopedTempDir
{
    std::filesystem::path path;

    FScopedTempDir()
    {
        path = std::filesystem::temp_directory_path() /
               std::format("ya_file_explorer_test_{}", std::rand());
        std::filesystem::create_directories(path);
    }

    ~FScopedTempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void touchFile(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path.string());
    out << "x";
}

} // namespace

TEST(FileExplorerNavigationTest, CollectEntriesSortsDirectoriesBeforeFilesAndHonorsSearch)
{
    FScopedTempDir temp;
    const std::filesystem::path mount = temp.path / "Mount";
    std::filesystem::create_directories(mount / "Alpha");
    std::filesystem::create_directories(mount / "Beta");
    touchFile(mount / "gamma.scene.json");
    touchFile(mount / "delta.txt");

    FileExplorer explorer;
    explorer.init({FileExplorer::MountPoint{.name = "Test", .path = mount}}, {".scene.json"});

    std::vector<FileExplorer::FEntry> entries;
    explorer.collectEntries(entries);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_TRUE(entries[0].bIsDirectory);
    EXPECT_EQ(entries[0].name, "Alpha");
    EXPECT_TRUE(entries[1].bIsDirectory);
    EXPECT_EQ(entries[1].name, "Beta");
    EXPECT_FALSE(entries[2].bIsDirectory);
    EXPECT_EQ(entries[2].name, "gamma.scene.json");

    explorer.setSearchText("bet");
    explorer.collectEntries(entries);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "Beta");
}

TEST(FileExplorerNavigationTest, NavigateBackStaysWithinActiveMountPoint)
{
    FScopedTempDir temp;
    const std::filesystem::path mount = temp.path / "Mount";
    const std::filesystem::path nested = mount / "A" / "B";
    std::filesystem::create_directories(nested);

    FileExplorer explorer;
    explorer.init({FileExplorer::MountPoint{.name = "Test", .path = mount}});
    EXPECT_EQ(explorer.getCurrentDirectory(), mount);

    EXPECT_TRUE(explorer.navigateInto(mount / "A"));
    EXPECT_EQ(explorer.getCurrentDirectory(), mount / "A");
    EXPECT_TRUE(explorer.navigateInto(nested));
    EXPECT_EQ(explorer.getCurrentDirectory(), nested);

    EXPECT_TRUE(explorer.navigateBack());
    EXPECT_EQ(explorer.getCurrentDirectory(), mount / "A");
    EXPECT_TRUE(explorer.navigateBack());
    EXPECT_EQ(explorer.getCurrentDirectory(), mount);
    EXPECT_FALSE(explorer.navigateBack());
    EXPECT_EQ(explorer.getCurrentDirectory(), mount);
}

TEST(FileExplorerNavigationTest, SelectMountPointAndSelectedPathRetargetCurrentDirectory)
{
    FScopedTempDir temp;
    const std::filesystem::path mountA = temp.path / "Engine";
    const std::filesystem::path mountB = temp.path / "Game";
    std::filesystem::create_directories(mountA / "Content");
    std::filesystem::create_directories(mountB / "Textures");
    touchFile(mountB / "Textures" / "brick.png");

    FileExplorer explorer;
    explorer.init({
        FileExplorer::MountPoint{.name = "Engine", .path = mountA},
        FileExplorer::MountPoint{.name = "Game", .path = mountB},
    });

    explorer.selectMountPoint(FileExplorer::MountPoint{.name = "Game", .path = mountB});
    ASSERT_NE(explorer.getActiveMountPoint(), nullptr);
    EXPECT_EQ(explorer.getActiveMountPoint()->name, "Game");
    EXPECT_EQ(explorer.getCurrentDirectory(), mountB);

    explorer.setSelectedPath(mountB / "Textures" / "brick.png");
    EXPECT_EQ(explorer.getCurrentDirectory(), mountB / "Textures");
    EXPECT_EQ(explorer.getSelectedPath(), mountB / "Textures" / "brick.png");
    ASSERT_NE(explorer.getActiveMountPoint(), nullptr);
    EXPECT_EQ(explorer.getActiveMountPoint()->name, "Game");
}

TEST(FileExplorerNavigationTest, GetSearchTextRoundTripsSetSearchText)
{
    FileExplorer explorer;
    explorer.setSearchText("Brick");
    EXPECT_EQ(explorer.getSearchText(), "Brick");

    explorer.setSearchText("");
    EXPECT_TRUE(explorer.getSearchText().empty());
}

} // namespace ya
