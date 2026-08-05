#include "Runtime/Rendering/Deferred/DeferredRenderPipeline.h"

#include "Config/ConfigManager.h"
#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST(DeferredFrameGraphResourcesTest, KeepsOptionalInputsExplicitAndHandlesFrameLocal)
{
    DeferredFrameGraphResources resources{};

    EXPECT_FALSE(resources.buffers.ssaoFrame.has_value());
    EXPECT_FALSE(resources.textures.ssao.has_value());
    EXPECT_FALSE(resources.textures.environmentCubemap.has_value());
    EXPECT_FALSE(resources.textures.shadowDepth.has_value());
    EXPECT_FALSE(resources.textures.postprocessOutput.has_value());
    EXPECT_FALSE(resources.passes.shadow.shadowDepth.has_value());

    resources.buffers.frame = RGBufferHandle{.index = 2, .generation = 7};
    resources.textures.gBufferColors[0] = RGTextureHandle{.index = 3, .generation = 9};
    resources.textures.ssao = RGTextureHandle{.index = 4, .generation = 11};
    resources.passes.gBuffer = RGPassHandle{.index = 5, .generation = 13};

    EXPECT_TRUE(resources.buffers.frame.isValid());
    EXPECT_TRUE(resources.textures.gBufferColors[0].isValid());
    ASSERT_TRUE(resources.textures.ssao.has_value());
    EXPECT_EQ(resources.textures.ssao->index, 4u);
    ASSERT_TRUE(resources.passes.gBuffer.has_value());
    EXPECT_EQ(resources.passes.gBuffer->generation, 13u);
}

TEST(DeferredGBufferPassParamsTest, DefaultsAreEmptyAndHandlesRemainFrameLocal)
{
    DeferredGBufferPassParams params{};

    EXPECT_FALSE(params.frame.handle.isValid());
    EXPECT_FALSE(params.light.handle.isValid());
    EXPECT_FALSE(params.skinning.isValid());
    EXPECT_EQ(params.frame.range.offset, 0u);
    EXPECT_EQ(params.frame.range.size, 0u);
    EXPECT_EQ(params.layerCount, 1u);
    EXPECT_FALSE(params.frameAndLightDescriptorSet);
    EXPECT_FALSE(params.skinningDescriptorSet);
    for (const auto& color : params.gBufferColors) {
        EXPECT_FALSE(color.isValid());
    }
    EXPECT_FALSE(params.gBufferDepth.isValid());

    params.frame.handle = RGBufferHandle{.index = 2, .generation = 7};
    params.frame.range  = RGBufferRange{.offset = 256, .size = 128};
    params.light.handle = RGBufferHandle{.index = 3, .generation = 8};
    params.skinning     = RGBufferHandle{.index = 4, .generation = 9};
    params.gBufferColors[0] = RGTextureHandle{.index = 5, .generation = 10};
    params.gBufferDepth     = RGTextureHandle{.index = 6, .generation = 11};
    params.renderArea       = Rect2D{.pos = {0, 0}, .extent = {1920, 1080}};

    EXPECT_TRUE(params.frame.handle.isValid());
    EXPECT_EQ(params.frame.range.offset, 256u);
    EXPECT_EQ(params.frame.range.size, 128u);
    EXPECT_TRUE(params.light.handle.isValid());
    EXPECT_TRUE(params.skinning.isValid());
    EXPECT_TRUE(params.gBufferColors[0].isValid());
    EXPECT_TRUE(params.gBufferDepth.isValid());
    EXPECT_EQ(params.renderArea.extent.x, 1920);
    EXPECT_EQ(params.renderArea.extent.y, 1080);
}

TEST(DeferredPassParamsTest, SSAOAndLightDefaultsAreEmptyAndHandlesRemainFrameLocal)
{
    DeferredSSAOPassParams ssao{};
    EXPECT_FALSE(ssao.frame.isValid());
    EXPECT_FALSE(ssao.albedo.isValid());
    EXPECT_FALSE(ssao.normal.isValid());
    EXPECT_FALSE(ssao.depth.isValid());
    EXPECT_FALSE(ssao.output.isValid());
    EXPECT_EQ(ssao.frameRange.offset, 0u);
    EXPECT_EQ(ssao.frameRange.size, 0u);
    EXPECT_FALSE(ssao.frameDescriptorSet);

    ssao.frame  = RGBufferHandle{.index = 1, .generation = 2};
    ssao.albedo = RGTextureHandle{.index = 3, .generation = 4};
    EXPECT_TRUE(ssao.frame.isValid());
    EXPECT_TRUE(ssao.albedo.isValid());

    DeferredLightPassParams light{};
    EXPECT_FALSE(light.frame.handle.isValid());
    EXPECT_FALSE(light.light.handle.isValid());
    EXPECT_FALSE(light.gBufferDepth.isValid());
    EXPECT_FALSE(light.ssao.has_value());
    EXPECT_FALSE(light.viewportColor.isValid());
    EXPECT_EQ(light.layerCount, 1u);
    for (const auto& color : light.gBufferColors) {
        EXPECT_FALSE(color.isValid());
    }

    light.frame.handle      = RGBufferHandle{.index = 5, .generation = 6};
    light.gBufferColors[0]  = RGTextureHandle{.index = 7, .generation = 8};
    light.gBufferDepth      = RGTextureHandle{.index = 8, .generation = 9};
    light.ssao              = RGTextureHandle{.index = 9, .generation = 10};
    EXPECT_TRUE(light.frame.handle.isValid());
    EXPECT_TRUE(light.gBufferColors[0].isValid());
    EXPECT_TRUE(light.gBufferDepth.isValid());
    ASSERT_TRUE(light.ssao.has_value());
    EXPECT_EQ(light.ssao->index, 9u);
}

TEST(DeferredPassParamsTest, SkyboxOverlayDefaultsAreEmptyAndCallbacksRemainExplicit)
{
    DeferredSkyboxPassParams skybox{};
    EXPECT_FALSE(skybox.frame.handle.isValid());
    EXPECT_FALSE(skybox.viewportColor.isValid());
    EXPECT_FALSE(skybox.depth.isValid());
    EXPECT_EQ(skybox.layerCount, 1u);
    EXPECT_FALSE(skybox.skybox.bAvailable);
    EXPECT_FALSE(skybox.skybox.frameDescriptorSet);
    EXPECT_FALSE(skybox.skybox.descriptorSet);
    EXPECT_EQ(skybox.skybox.mesh, nullptr);

    DeferredForwardTransparentPassParams transparent{};
    EXPECT_FALSE(transparent.color.isValid());
    EXPECT_FALSE(transparent.depth.isValid());
    EXPECT_EQ(transparent.layerCount, 1u);
    EXPECT_TRUE(transparent.overlay.billboards.empty());
    EXPECT_TRUE(transparent.overlay.directionGizmos.empty());

    DeferredOverlayPassParams overlay{};
    EXPECT_FALSE(overlay.color.isValid());
    EXPECT_FALSE(overlay.depth.isValid());
    EXPECT_EQ(overlay.layerCount, 1u);
    EXPECT_FALSE(overlay.overlaySnapshot);
}

TEST(PostProcessingStageTest, FinalizeParamsDefaultsStayEmpty)
{
    PostProcessingStage::FinalizePassParams params{};
    EXPECT_FALSE(params.input.isValid());
    EXPECT_FALSE(params.output.isValid());
    EXPECT_EQ(params.inputExtent.width, 0u);
    EXPECT_EQ(params.inputExtent.height, 0u);
    EXPECT_FALSE(params.bOutputIsSRGB);
    EXPECT_EQ(params.postContext, nullptr);
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
