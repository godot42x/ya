#include "GameEditor/UI/EditorFilePickerDialog.h"

#include "GUI/Widgets/WidgetTree.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

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
               ("ya_file_picker_dialog_test_" + std::to_string(std::rand()));
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

TEST(EditorFilePickerDialogTest, SelectionValidRequiresMatchingEntryKind)
{
    const std::filesystem::path file{"/tmp/a.png"};
    const std::filesystem::path dir{"/tmp/folder"};
    const std::vector<FileExplorer::FEntry> entries{
        FileExplorer::FEntry{.path = file, .name = "a.png", .bIsDirectory = false},
        FileExplorer::FEntry{.path = dir, .name = "folder", .bIsDirectory = true},
    };
    EXPECT_TRUE(isRetainedPickerSelectionValid(entries, file, FileExplorer::SelectionMode::File));
    EXPECT_FALSE(isRetainedPickerSelectionValid(entries, dir, FileExplorer::SelectionMode::File));
    EXPECT_TRUE(isRetainedPickerSelectionValid(entries, dir, FileExplorer::SelectionMode::Directory));
    EXPECT_FALSE(isRetainedPickerSelectionValid(entries, file, FileExplorer::SelectionMode::Directory));
}

TEST(EditorFilePickerDialogTest, PickModeConfirmsSelectedFile)
{
    FScopedTempDir temp;
    const std::filesystem::path mount = temp.path / "Mount";
    touchFile(mount / "albedo.png");

    WidgetTree tree({.width = 800, .height = 600});
    EditorFilePickerDialog dialog;
    std::string picked;
    FEditorFilePickerRequest request;
    request.title = "Select Texture";
    request.extensions = {".png"};
    request.mounts = {FileExplorer::MountPoint{.name = "Test", .path = mount}};
    request.onPicked = [&](std::string path) { picked = std::move(path); };

    dialog.open(tree, request);
    ASSERT_TRUE(dialog.isOpen());
    dialog.selectPath(mount / "albedo.png");
    dialog.sync(tree);
    EXPECT_TRUE(dialog.confirm());
    EXPECT_EQ(std::filesystem::path(picked).filename(), "albedo.png");
    EXPECT_FALSE(dialog.isOpen());
}

TEST(EditorFilePickerDialogTest, SaveAsModeComposesDirectoryAndName)
{
    FScopedTempDir temp;
    const std::filesystem::path mount = temp.path / "Scenes";
    std::filesystem::create_directories(mount);

    WidgetTree tree({.width = 800, .height = 600});
    EditorFilePickerDialog dialog;
    std::string picked;
    FEditorFilePickerRequest request = makeSceneSavePickerRequest(
        "Forest",
        mount.string(),
        [&](std::string path) { picked = std::move(path); });
    request.configScope.clear();
    request.mounts = {FileExplorer::MountPoint{.name = "Test", .path = mount}};
    dialog.open(tree, std::move(request));
    dialog.selectPath(mount);
    dialog.setSaveAsName("Castle");
    dialog.sync(tree);
    EXPECT_TRUE(dialog.confirm());
    EXPECT_EQ(std::filesystem::path(picked).filename(), "Castle.scene.json");
    EXPECT_EQ(std::filesystem::path(picked).parent_path(), mount);
}

} // namespace ya
