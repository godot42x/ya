#include "Runtime/App/DeferredRender/DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace ya
{

class DeferredRenderPipelineTestAccess
{
  public:
    static void applyPendingSettings(DeferredRenderPipeline& pipeline)
    {
        pipeline.applyPendingSettings();
    }

    static void loadPersistentSettings(DeferredRenderPipeline& pipeline)
    {
        pipeline.loadPersistentSettings();
    }
};

namespace
{

class DeferredRenderPipelineSettingsTest : public ::testing::Test
{
  protected:
    std::filesystem::path _originalCwd;
    std::filesystem::path _tempRoot;

    void SetUp() override
    {
        _originalCwd = std::filesystem::current_path();
        _tempRoot    = std::filesystem::temp_directory_path() /
                    std::filesystem::path("ya-deferred-settings-test-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                                          std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        std::filesystem::remove_all(_tempRoot);
        std::filesystem::create_directories(_tempRoot);
        std::filesystem::current_path(_tempRoot);

        VirtualFileSystem::init();
        ConfigManager::get().init();
        ConfigManager::get().openDocument("runtime",
                                          "Engine/Saved/Config/Runtime.json",
                                          {.bPersistIfMissing = false, .bReadOnly = false});
    }

    void TearDown() override
    {
        ConfigManager::get().shutdown();
        std::filesystem::current_path(_originalCwd);
        std::filesystem::remove_all(_tempRoot);
    }
};

TEST(DeferredRenderPipelineTest, SettingsCommandsApplyLatestSnapshotAtFrameBoundary)
{
    DeferredRenderPipeline pipeline;

    auto first = pipeline.buildSettingsSnapshot();
    first.bReverseViewportY           = false;
    first.bSSAOEnabled                = false;
    first.postProcessing.bEnableBloom = true;
    first.shadow                      = ShadowSettings::fromQuality(EShadowQuality::Low);
    pipeline.requestSettings(first);

    auto latest = pipeline.buildSettingsSnapshot();
    latest.bReverseViewportY              = false;
    latest.bSSAOEnabled                   = true;
    latest.postProcessing.bEnableInversion = true;
    latest.postProcessing.bEnableBloom  = false;
    latest.shadow                       = ShadowSettings::fromQuality(EShadowQuality::Ultra);
    pipeline.requestSettings(latest);

    const auto beforeApply = pipeline.buildSettingsSnapshot();
    EXPECT_TRUE(beforeApply.bReverseViewportY);
    EXPECT_TRUE(beforeApply.bSSAOEnabled);
    EXPECT_FALSE(beforeApply.postProcessing.bEnableInversion);
    EXPECT_EQ(beforeApply.shadow.quality, EShadowQuality::Off);

    DeferredRenderPipelineTestAccess::applyPendingSettings(pipeline);

    const auto afterApply = pipeline.buildSettingsSnapshot();
    EXPECT_FALSE(afterApply.bReverseViewportY);
    EXPECT_TRUE(afterApply.bSSAOEnabled);
    EXPECT_TRUE(afterApply.postProcessing.bEnableInversion);
    EXPECT_FALSE(afterApply.postProcessing.bEnableBloom);
    EXPECT_EQ(afterApply.shadow.quality, EShadowQuality::Ultra);
}

TEST_F(DeferredRenderPipelineSettingsTest, PersistentShadowSettingsSeedFirstFrameState)
{
    ConfigManager::get().set("runtime", "render.deferred.shadow.enableShadowMapping", true);
    ConfigManager::get().set("runtime", "render.deferred.shadow.quality", static_cast<int>(EShadowQuality::Ultra));

    ShadowSettings runtimeShadowSettings = ShadowSettings::fromQuality(EShadowQuality::Low);
    DeferredRenderPipeline pipeline;
    pipeline._shadowSettings = &runtimeShadowSettings;

    DeferredRenderPipelineTestAccess::loadPersistentSettings(pipeline);

    EXPECT_EQ(runtimeShadowSettings.quality, EShadowQuality::Ultra);
    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.quality, EShadowQuality::Ultra);
}

} // namespace
} // namespace ya
