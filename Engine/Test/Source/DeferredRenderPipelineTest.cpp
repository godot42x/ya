#include "Runtime/Rendering/Deferred/DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <limits>
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

class DeferredFrameResourceSetTestAccess
{
  public:
    static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount)
    {
        return DeferredFrameResourceSet::calculateSkinningCapacity(
            currentCapacity,
            paletteCount);
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

TEST(DeferredFrameResourceSetTest, SkinningCapacityStartsSmallGrowsAndRejectsOverflow)
{
    const auto initialCapacity =
        DeferredFrameResourceSetTestAccess::calculateSkinningCapacity(0, 0);
    ASSERT_TRUE(initialCapacity.has_value());
    EXPECT_EQ(*initialCapacity, 16u);

    const auto grownCapacity =
        DeferredFrameResourceSetTestAccess::calculateSkinningCapacity(16, 17);
    ASSERT_TRUE(grownCapacity.has_value());
    EXPECT_EQ(*grownCapacity, 32u);

    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    EXPECT_FALSE(
        DeferredFrameResourceSetTestAccess::calculateSkinningCapacity(
            maxPaletteCount,
            maxPaletteCount + 1u)
            .has_value());
}

TEST(SSAOStageTest, BuildsFrameDataWithoutOwningGpuResources)
{
    SSAOStage stage;
    stage.setSettings(1.25f, 0.04f, 1.75f, 2.0f, false);

    RenderFrameData frameData{};
    frameData.projection = glm::mat4(2.0f);
    frameData.view       = glm::mat4(1.0f);
    RenderStageContext ctx{
        .frameData      = &frameData,
        .viewportExtent = {.width = 640, .height = 480},
    };

    const auto payload = stage.buildFrameData(ctx);
    EXPECT_EQ(payload.screenResolution.x, 640);
    EXPECT_EQ(payload.screenResolution.y, 480);
    EXPECT_FLOAT_EQ(payload.radius, 1.25f);
    EXPECT_FLOAT_EQ(payload.bias, 0.04f);
    EXPECT_FLOAT_EQ(payload.power, 1.75f);
    EXPECT_FLOAT_EQ(payload.intensity, 2.0f);
    EXPECT_EQ(payload.reverseY, 0u);
    EXPECT_FLOAT_EQ(payload.projectMat[0][0], 2.0f);
    EXPECT_FLOAT_EQ(payload.invProjectMat[0][0], 0.5f);
}

TEST(ViewportOverlayStageTest, BuildsSkyboxFrameDataWithoutCameraTranslation)
{
    ViewportOverlayStage stage;

    RenderFrameData frameData{};
    frameData.projection = glm::mat4(2.0f);
    frameData.view       = glm::mat4(1.0f);
    frameData.view[3]    = glm::vec4(5.0f, 6.0f, 7.0f, 1.0f);
    RenderStageContext ctx{
        .frameData = &frameData,
    };

    const auto payload = stage.buildSkyboxFrameData(ctx);
    EXPECT_FLOAT_EQ(payload.proj[0][0], 2.0f);
    EXPECT_FLOAT_EQ(payload.view[3][0], 0.0f);
    EXPECT_FLOAT_EQ(payload.view[3][1], 0.0f);
    EXPECT_FLOAT_EQ(payload.view[3][2], 0.0f);
    EXPECT_FLOAT_EQ(payload.view[3][3], 1.0f);
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
