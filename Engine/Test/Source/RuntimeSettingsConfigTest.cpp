#include "Runtime/Rendering/Common/Shadow/Common/ShadowSettingsConfig.h"

#include "Config/ConfigManager.h"
#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace ya
{
namespace
{

constexpr const char* RUNTIME_CONFIG_PATH = "Engine/Saved/Config/Runtime.json";

class RuntimeSettingsConfigTest : public ::testing::Test
{
  protected:
    std::filesystem::path _originalCwd;
    std::filesystem::path _tempRoot;

    void SetUp() override
    {
        _originalCwd = std::filesystem::current_path();
        _tempRoot    = std::filesystem::temp_directory_path() /
                    std::filesystem::path("ya-runtime-settings-config-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
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

    void writeRuntimeConfig(std::string_view content)
    {
        const auto path = _tempRoot / RUNTIME_CONFIG_PATH;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path);
        ASSERT_TRUE(stream.is_open());
        stream << content;
    }
};

TEST_F(RuntimeSettingsConfigTest, LoadsDeveloperSettingsFromRuntimeDocumentWithoutEditorDocument)
{
    writeRuntimeConfig(R"({
  "render": {
    "deferred": {
      "shadow": {
        "quality": 4,
        "resolution": 4096,
        "directionalEnabled": false,
        "pointLightEnabled": true,
        "filter": 2,
        "bias": 0.025,
        "normalBias": 0.075,
        "directionalDistance": 250.0,
        "directionalCascades": 3,
        "directionalCascadeSplitRatios": [0.05, 0.3, 0.8],
        "directionalDepthRangeMultiplier": 12.0
      }
    }
  }
})");

    ConfigManager::get().openDocument("runtime", RUNTIME_CONFIG_PATH);

    const ShadowSettings baseline = ShadowSettings::fromQuality(EShadowQuality::Low);
    const ShadowSettings settings = shadow_settings::loadRuntimeSettings(baseline);

    EXPECT_TRUE(shadow_settings::hasRuntimeSettings());
    EXPECT_FALSE(ConfigManager::get().hasDocument("editor"));
    EXPECT_EQ(settings.quality, EShadowQuality::Ultra);
    EXPECT_EQ(settings.resolution, 4096u);
    EXPECT_FALSE(settings.directionalEnabled);
    EXPECT_TRUE(settings.pointLightEnabled);
    EXPECT_EQ(settings.filter, EShadowFilter::PCF_High);
    EXPECT_FLOAT_EQ(settings.bias, 0.025f);
    EXPECT_FLOAT_EQ(settings.normalBias, 0.075f);
    EXPECT_FLOAT_EQ(settings.directionalDistance, 250.0f);
    EXPECT_EQ(settings.directionalCascades, 3u);
    EXPECT_FLOAT_EQ(settings.directionalCascadeSplitRatios[0], 0.05f);
    EXPECT_FLOAT_EQ(settings.directionalCascadeSplitRatios[1], 0.3f);
    EXPECT_FLOAT_EQ(settings.directionalCascadeSplitRatios[2], 0.8f);
    EXPECT_FLOAT_EQ(settings.directionalDepthRangeMultiplier, 12.0f);
}

} // namespace
} // namespace ya
