#include "Runtime/App/Lifecycle/AppAutomation.h"

#include "Config/ConfigManager.h"
#include "Core/Profiling/Profiling.h"
#include "Core/System/VirtualFileSystem.h"
#include "Runtime/App/App.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace ya
{
namespace
{

constexpr const char* DEFAULT_AUTOMATION_CONFIG_PATH = "Engine/Saved/Config/Automation.json";
constexpr const char* CUSTOM_AUTOMATION_CONFIG_PATH  = "Engine/Saved/Config/Automation.custom.json";

class AppAutomationConfigTest : public ::testing::Test
{
  protected:
    std::filesystem::path _originalCwd;
    std::filesystem::path _tempRoot;

    void SetUp() override
    {
        _originalCwd = std::filesystem::current_path();
        _tempRoot    = std::filesystem::temp_directory_path() /
                    std::filesystem::path("ya-app-automation-config-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                                          std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));

        std::filesystem::remove_all(_tempRoot);
        std::filesystem::create_directories(_tempRoot);
        std::filesystem::current_path(_tempRoot);

        VirtualFileSystem::init();
        ConfigManager::get().init();
    }

    void TearDown() override
    {
        ConfigManager::get().shutdown();
        std::filesystem::current_path(_originalCwd);
        std::filesystem::remove_all(_tempRoot);
    }

    static void writeJsonFile(const std::filesystem::path& path, std::string_view content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path);
        ASSERT_TRUE(stream.is_open());
        stream << content;
        stream.close();
    }

    void writeAutomationConfig(const std::string& relativePath, std::string_view content)
    {
        writeJsonFile(_tempRoot / relativePath, content);
    }
};

TEST_F(AppAutomationConfigTest, LoadConfigUsesExplicitAutomationConfigPath)
{
    writeAutomationConfig(DEFAULT_AUTOMATION_CONFIG_PATH,
                          R"({
  "screenshot": { "target": "viewport" },
  "profile": {
    "cpu": { "enabled": false, "output": "default.cpu.json" },
    "sessionName": "DefaultSession",
    "gpu": { "renderdoc": false, "outputDir": "Engine/Saved/RenderDocDefault" }
  },
  "shadow": { "quality": "low", "directionalEnabled": false }
})");
    writeAutomationConfig(CUSTOM_AUTOMATION_CONFIG_PATH,
                          R"({
  "screenshot": { "target": "editor" },
  "profile": {
    "cpu": { "enabled": true, "output": "custom.cpu.json" },
    "sessionName": "CustomSession",
    "gpu": { "renderdoc": true, "outputDir": "Engine/Saved/RenderDocCustom" }
  },
  "shadow": { "quality": "ultra", "directionalEnabled": true }
})");

    AppDesc appDesc;
    appDesc.automation.configPath = CUSTOM_AUTOMATION_CONFIG_PATH;

    AppAutomation::loadConfig(appDesc);
    profiling::applyAppOverrides(appDesc);
    AppAutomation::applyStartupOverrides(appDesc);

    ASSERT_TRUE(ConfigManager::get().hasDocument("automation"));
    const auto* automationDoc = ConfigManager::get().findDocument("automation");
    ASSERT_NE(automationDoc, nullptr);
    EXPECT_EQ(automationDoc->path, CUSTOM_AUTOMATION_CONFIG_PATH);

    EXPECT_TRUE(appDesc.profiling.bCpuProfileEnabled);
    ASSERT_TRUE(appDesc.profiling.cpuProfileOutputPath.has_value());
    EXPECT_EQ(*appDesc.profiling.cpuProfileOutputPath, "custom.cpu.json");
    EXPECT_EQ(appDesc.profiling.profileSessionName, "CustomSession");

    EXPECT_TRUE(appDesc.automation.renderDocCapture);
    EXPECT_EQ(appDesc.renderDocCaptureOutputDir, "Engine/Saved/RenderDocCustom");
    EXPECT_EQ(appDesc.automation.screenshotTarget, EAutomationScreenshotTarget::Editor);
    ASSERT_TRUE(appDesc.automation.shadow.quality.has_value());
    EXPECT_EQ(*appDesc.automation.shadow.quality, EShadowQuality::Ultra);
    ASSERT_TRUE(appDesc.automation.shadow.directionalEnabled.has_value());
    EXPECT_TRUE(*appDesc.automation.shadow.directionalEnabled);
}

TEST_F(AppAutomationConfigTest, LoadConfigDefaultsToStandardAutomationConfigPath)
{
    writeAutomationConfig(DEFAULT_AUTOMATION_CONFIG_PATH,
                          R"({
  "screenshot": { "target": "editor" },
  "profile": {
    "cpu": { "enabled": true, "output": "default.cpu.json" },
    "sessionName": "DefaultSession",
    "gpu": { "renderdoc": true, "outputDir": "Engine/Saved/RenderDocDefault" }
  },
  "shadow": { "quality": "high", "directionalEnabled": true }
})");

    AppDesc appDesc;

    AppAutomation::loadConfig(appDesc);
    profiling::applyAppOverrides(appDesc);
    AppAutomation::applyStartupOverrides(appDesc);

    ASSERT_TRUE(ConfigManager::get().hasDocument("automation"));
    const auto* automationDoc = ConfigManager::get().findDocument("automation");
    ASSERT_NE(automationDoc, nullptr);
    EXPECT_EQ(automationDoc->path, DEFAULT_AUTOMATION_CONFIG_PATH);

    EXPECT_TRUE(appDesc.profiling.bCpuProfileEnabled);
    ASSERT_TRUE(appDesc.profiling.cpuProfileOutputPath.has_value());
    EXPECT_EQ(*appDesc.profiling.cpuProfileOutputPath, "default.cpu.json");
    EXPECT_EQ(appDesc.profiling.profileSessionName, "DefaultSession");

    EXPECT_TRUE(appDesc.automation.renderDocCapture);
    EXPECT_EQ(appDesc.renderDocCaptureOutputDir, "Engine/Saved/RenderDocDefault");
    EXPECT_EQ(appDesc.automation.screenshotTarget, EAutomationScreenshotTarget::Editor);
    ASSERT_TRUE(appDesc.automation.shadow.quality.has_value());
    EXPECT_EQ(*appDesc.automation.shadow.quality, EShadowQuality::High);
    ASSERT_TRUE(appDesc.automation.shadow.directionalEnabled.has_value());
    EXPECT_TRUE(*appDesc.automation.shadow.directionalEnabled);
}

} // namespace
} // namespace ya
