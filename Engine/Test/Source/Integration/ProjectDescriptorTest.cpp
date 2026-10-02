#include "App/Module/ProjectDescriptor.h"

#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace ya
{
namespace
{

class ProjectDescriptorTest : public ::testing::Test
{
  protected:
    std::filesystem::path _root;
    std::filesystem::path _originalCwd;

    void SetUp() override
    {
        _originalCwd = std::filesystem::current_path();
        _root = std::filesystem::temp_directory_path() /
                ("ya-project-descriptor-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                 std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(_root);
        std::filesystem::create_directories(_root);
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::current_path(_originalCwd, error);
        std::filesystem::remove_all(_root, error);
    }

    std::filesystem::path writeText(const std::filesystem::path& path, std::string_view content) const
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path);
        EXPECT_TRUE(stream.is_open());
        stream << content;
        return path;
    }

    std::filesystem::path writeModuleManifest() const
    {
        return writeText(_root / "Game.yamodule",
                         R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    }

    /// A standalone VFS whose "Engine" mount roots a private workspace: the
    /// bootstrap's mount contract without the process-wide engine tree.
    void initWorkspaceVfs(const std::filesystem::path& workspaceRoot) const
    {
        VirtualFileSystem::init();
        auto* vfs = VirtualFileSystem::get();
        ASSERT_NE(vfs, nullptr);
        vfs->mount("Engine", workspaceRoot / "Engine");
    }
};

TEST_F(ProjectDescriptorTest, LoadsAndValidatesProjectResources)
{
    writeText(_root / "Content" / "Scenes" / "Main.scene.json", R"({"version":"1.0","name":"Main","entities":[]})");
    writeText(_root / "Game.yamodule",
              R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "defaultScene": "Content/Scenes/Main.scene.json",
  "inputActions": {
    "look": ["MouseRight"]
  }
})");

    const auto descriptor = FProjectDescriptor::load(descriptorPath);
    EXPECT_EQ(descriptor.name, "Game");
    EXPECT_EQ(descriptor.mainModule, "Game");
    ASSERT_EQ(descriptor.modules.size(), 1u);
    EXPECT_TRUE(std::filesystem::is_regular_file(descriptor.modules.front()));
    ASSERT_TRUE(descriptor.defaultScene.has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(descriptor.resolvePath(*descriptor.defaultScene)));
    ASSERT_TRUE(descriptor.inputActions.contains("look"));
    EXPECT_FALSE(descriptor.icon.has_value());
    EXPECT_FALSE(descriptor.uiReferenceResolution.has_value());
}

TEST_F(ProjectDescriptorTest, LoadsUIReferenceResolution)
{
    writeText(_root / "Content" / ".keep", "");
    writeText(_root / "Game.yamodule",
              R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "uiReferenceResolution": [1280, 720]
})");

    const auto descriptor = FProjectDescriptor::load(descriptorPath);
    ASSERT_TRUE(descriptor.uiReferenceResolution.has_value());
    EXPECT_EQ(descriptor.uiReferenceResolution->width, 1280u);
    EXPECT_EQ(descriptor.uiReferenceResolution->height, 720u);

    const auto invalidPath = writeText(_root / "Bad.yaproject",
                                       R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "uiReferenceResolution": [0, 720]
})");
    EXPECT_THROW((void)FProjectDescriptor::load(invalidPath), std::runtime_error);
}

TEST_F(ProjectDescriptorTest, LoadsOptionalIcon)
{
    writeText(_root / "Content" / ".keep", "");
    writeText(_root / "Game.yamodule",
              R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    const auto branding = std::filesystem::path("Engine/Content/Branding/ya-icon.png");
    ASSERT_TRUE(std::filesystem::is_regular_file(branding));
    std::filesystem::copy_file(branding, _root / "Content" / "AppIcon.png");
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "icon": "Content/AppIcon.png"
})");

    const auto descriptor = FProjectDescriptor::load(descriptorPath);
    ASSERT_TRUE(descriptor.icon.has_value());
    // The icon is stored resolved (absolute): consumers never re-resolve it.
    EXPECT_TRUE(std::filesystem::path(*descriptor.icon).is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(*descriptor.icon));
    EXPECT_EQ(*descriptor.icon,
              (std::filesystem::weakly_canonical(_root) / "Content" / "AppIcon.png").lexically_normal().string());
}

TEST_F(ProjectDescriptorTest, RejectsMissingIcon)
{
    writeText(_root / "Content" / ".keep", "");
    writeText(_root / "Game.yamodule",
              R"({
  "schemaVersion": 1,
  "name": "Game",
  "kind": "project",
  "binary": "Game",
  "dependencies": []
})");
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "icon": "Content/MissingIcon.png"
})");

    EXPECT_THROW(
        {
            try {
                (void)FProjectDescriptor::load(descriptorPath);
            }
            catch (const std::runtime_error& error) {
                EXPECT_NE(std::string(error.what()).find("icon not found"), std::string::npos);
                throw;
            }
        },
        std::runtime_error);
}

TEST_F(ProjectDescriptorTest, RejectsMissingDefaultScene)
{
    writeText(_root / "Content" / ".keep", "");
    writeModuleManifest();
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "defaultScene": "Content/Scenes/Missing.scene.json"
})");

    EXPECT_THROW(
        {
            try {
                (void)FProjectDescriptor::load(descriptorPath);
            }
            catch (const std::runtime_error& error) {
                EXPECT_NE(std::string(error.what()).find("defaultScene not found"), std::string::npos);
                throw;
            }
        },
        std::runtime_error);
}

TEST_F(ProjectDescriptorTest, PrefersProjectRelativeOverWorkspaceRelative)
{
    writeModuleManifest();
    writeText(_root / "Content" / ".keep", "");
    // The same relative file lives in the project root and at the workspace
    // root (mounted here as a private VFS "Engine"): the project one wins.
    const auto workspaceRoot = std::filesystem::weakly_canonical(_root) / "workspace";
    writeText(workspaceRoot / "Content" / "Shared.txt", "workspace");
    writeText(_root / "Content" / "Shared.txt", "project");
    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "icon": "Content/Shared.txt"
})");

    initWorkspaceVfs(workspaceRoot);
    const auto descriptor = FProjectDescriptor::load(descriptorPath);
    ASSERT_TRUE(descriptor.icon.has_value());
    const auto resolved = descriptor.resolvePath(*descriptor.icon);
    EXPECT_EQ(resolved, (std::filesystem::weakly_canonical(_root) / "Content" / "Shared.txt").lexically_normal());
}

TEST_F(ProjectDescriptorTest, ResolvesWorkspaceRelativePathsFromForeignCwd)
{
    writeModuleManifest();
    writeText(_root / "Content" / ".keep", "");
    // A workspace holding the engine tree (HelloMaterial.yaproject's
    // "Example/..." and "Engine/..." forms) and a project descriptor that
    // references it workspace-relatively. The process boots from a third
    // directory; the browser must still open the project.
    const auto workspaceRoot = std::filesystem::weakly_canonical(_root) / "workspace";
    writeText(workspaceRoot / "Engine" / "Content" / "Scenes" / "Ws.scene.json",
              R"({"version":"1.0","name":"Ws","entities":[]})");
    initWorkspaceVfs(workspaceRoot);

    const auto bootCwd = _root / "booted-elsewhere";
    std::filesystem::create_directories(bootCwd);
    std::filesystem::current_path(bootCwd);

    const auto descriptorPath = writeText(_root / "Game.yaproject",
                                          R"({
  "schemaVersion": 1,
  "name": "Game",
  "mainModule": "Game",
  "modules": ["Game.yamodule"],
  "plugins": [],
  "contentDir": "Content",
  "defaultScene": "Engine/Content/Scenes/Ws.scene.json"
})");

    const auto descriptor = FProjectDescriptor::load(descriptorPath);
    ASSERT_TRUE(descriptor.defaultScene.has_value());
    const auto resolved = std::filesystem::path(*descriptor.defaultScene);
    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved, (workspaceRoot / "Engine" / "Content" / "Scenes" / "Ws.scene.json").lexically_normal());
}

} // namespace
} // namespace ya
