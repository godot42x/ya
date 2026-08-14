#include "App/Module/ProjectDescriptor.h"

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

    void SetUp() override
    {
        _root = std::filesystem::temp_directory_path() /
                ("ya-project-descriptor-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                 std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(_root);
        std::filesystem::create_directories(_root);
    }

    void TearDown() override
    {
        std::error_code error;
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
}

TEST_F(ProjectDescriptorTest, RejectsMissingDefaultScene)
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

} // namespace
} // namespace ya
