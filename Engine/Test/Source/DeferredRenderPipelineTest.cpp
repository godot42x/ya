#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Deferred/DeferredFrameGraphPasses.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Render3D/Common/SceneFamilyResources.h"

#include "Core/Config/ConfigManager.h"
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

    static void publishViewResources(DeferredRenderPipeline& pipeline,
                                     const ViewResourceKey&  key,
                                     DeferredPipelineDebugViews views)
    {
        pipeline.publishViewResources(key, std::move(views));
    }

    static void reconcilePublishedViews(DeferredRenderPipeline& pipeline, const SceneRenderPlan& plan)
    {
        pipeline.reconcilePublishedViews(plan);
    }
};

class DeferredFrameResourceSetTestAccess
{
  public:
    static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount)
    {
        return calculateSceneFamilySkinningCapacity(currentCapacity, paletteCount);
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
    first.ssaoRadius                  = 0.2f;
    first.ssaoBias                    = 0.01f;
    first.bPBRDiffuseIBL              = false;
    first.postProcessing.bEnableBloom = true;
    first.shadow                      = ShadowSettings::fromQuality(EShadowQuality::Low);
    pipeline.requestSettings(first);

    auto latest = pipeline.buildSettingsSnapshot();
    latest.bReverseViewportY              = false;
    latest.bSSAOEnabled                   = true;
    latest.ssaoRadius                     = 1.5f;
    latest.ssaoPower                      = 2.0f;
    latest.ssaoIntensity                  = 3.0f;
    latest.bPBRDiffuseIBL                 = false;
    latest.bPBRSpecularIBL                = false;
    latest.postProcessing.bEnableInversion = true;
    latest.postProcessing.bEnableBloom  = false;
    latest.shadow                       = ShadowSettings::fromQuality(EShadowQuality::Ultra);
    pipeline.requestSettings(latest);

    const auto beforeApply = pipeline.buildSettingsSnapshot();
    EXPECT_TRUE(beforeApply.bReverseViewportY);
    EXPECT_TRUE(beforeApply.bSSAOEnabled);
    EXPECT_FLOAT_EQ(beforeApply.ssaoRadius, 0.6f);
    EXPECT_TRUE(beforeApply.bPBRDiffuseIBL);
    EXPECT_TRUE(beforeApply.bPBRSpecularIBL);
    EXPECT_FALSE(beforeApply.postProcessing.bEnableInversion);
    EXPECT_EQ(beforeApply.shadow.quality, EShadowQuality::Off);

    const auto pending = pipeline.resolveSettings();
    EXPECT_FALSE(pending.bReverseViewportY);
    EXPECT_TRUE(pending.bSSAOEnabled);
    EXPECT_FLOAT_EQ(pending.ssaoRadius, 1.5f);
    EXPECT_FALSE(pending.bPBRDiffuseIBL);
    EXPECT_FALSE(pending.bPBRSpecularIBL);

    DeferredRenderPipelineTestAccess::applyPendingSettings(pipeline);

    const auto afterApply = pipeline.buildSettingsSnapshot();
    EXPECT_FALSE(afterApply.bReverseViewportY);
    EXPECT_TRUE(afterApply.bSSAOEnabled);
    EXPECT_FLOAT_EQ(afterApply.ssaoRadius, 1.5f);
    EXPECT_FLOAT_EQ(afterApply.ssaoPower, 2.0f);
    EXPECT_FLOAT_EQ(afterApply.ssaoIntensity, 3.0f);
    EXPECT_FALSE(afterApply.bPBRDiffuseIBL);
    EXPECT_FALSE(afterApply.bPBRSpecularIBL);
    EXPECT_TRUE(afterApply.postProcessing.bEnableInversion);
    EXPECT_FALSE(afterApply.postProcessing.bEnableBloom);
    EXPECT_EQ(afterApply.shadow.quality, EShadowQuality::Ultra);
}

constexpr FRenderFeatureMask kGameFeatures    = toMask(ERenderFeature::Game);
constexpr Extent2D           kWorldExtent     = {.width = 1280, .height = 720};
constexpr Extent2D           kThumbnailExtent = {.width = 256, .height = 256};

DeferredAttachmentFormats deferredViewFormats()
{
    return DeferredAttachmentFormats{
        .colorFormats = {EFormat::R16G16B16A16_SFLOAT},
        .depthFormat  = EFormat::D32_SFLOAT,
    };
}

DeferredPipelineDebugViews makeDeferredViews(const std::shared_ptr<RenderTexture>& color,
                                             const std::shared_ptr<RenderTexture>& depth,
                                             const std::shared_ptr<RenderTexture>& entityId)
{
    DeferredPipelineDebugViews views{};
    views.viewportResources.publish(color, depth, entityId, deferredViewFormats());
    return views;
}

ViewResourceKey makeDeferredKey(SceneViewId viewId, Extent2D extent)
{
    return ViewResourceKey{
        .viewId      = viewId,
        .extent      = extent,
        .colorFormat = EFormat::R16G16B16A16_SFLOAT,
        .depthFormat = EFormat::D32_SFLOAT,
        .featureMask = kGameFeatures,
    };
}

/// The acceptance evidence for this batch on the deferred side: one tick
/// records a world View and a thumbnail View of different sizes, and each keeps
/// its own viewport attachments.
TEST(DeferredRenderPipelineTest, TwoViewsKeepTheirOwnPublishedResources)
{
    DeferredRenderPipeline pipeline;

    const auto worldColor     = std::make_shared<RenderTexture>();
    const auto worldDepth     = std::make_shared<RenderTexture>();
    const auto worldEntityId  = std::make_shared<RenderTexture>();
    const auto thumbColor     = std::make_shared<RenderTexture>();
    const auto thumbDepth     = std::make_shared<RenderTexture>();
    const auto thumbEntityId  = std::make_shared<RenderTexture>();

    DeferredRenderPipelineTestAccess::publishViewResources(
        pipeline, makeDeferredKey(11, kWorldExtent), makeDeferredViews(worldColor, worldDepth, worldEntityId));
    DeferredRenderPipelineTestAccess::publishViewResources(
        pipeline, makeDeferredKey(12, kThumbnailExtent), makeDeferredViews(thumbColor, thumbDepth, thumbEntityId));

    const DeferredPipelineDebugViews world     = pipeline.buildDebugViews(11);
    const DeferredPipelineDebugViews thumbnail = pipeline.buildDebugViews(12);
    EXPECT_EQ(world.viewportResources.colorOwner, worldColor);
    EXPECT_EQ(world.viewportResources.depthOwner, worldDepth);
    EXPECT_EQ(world.viewportResources.entityIdOwner, worldEntityId);
    EXPECT_EQ(thumbnail.viewportResources.colorOwner, thumbColor);
    EXPECT_EQ(thumbnail.viewportResources.depthOwner, thumbDepth);
    EXPECT_NE(world.viewportResources.colorOwner, thumbnail.viewportResources.colorOwner);

    // Identity-carrying queries answer about the named View, and a View this
    // pipeline never recorded answers nothing rather than another View's set.
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), worldDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(12), thumbDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(99), nullptr);
    EXPECT_EQ(pipeline.getEntityIdImageShared(11), worldEntityId);
    EXPECT_EQ(pipeline.buildDebugViews(99).viewportResources.colorOwner, nullptr);

    RenderTargetCatalog catalog;
    pipeline.appendRenderTargetEntries(catalog);
    // A GBuffer and a view target per recorded View, plus the shadow map.
    ASSERT_EQ(catalog.entries.size(), 5u);
    EXPECT_EQ(catalog.entries[4].owner, RenderTargetCatalog::Entry::EOwner::DeferredShadow);
    EXPECT_EQ(catalog.entries[0].owner, RenderTargetCatalog::Entry::EOwner::DeferredGBuffer);
    EXPECT_EQ(catalog.entries[1].owner, RenderTargetCatalog::Entry::EOwner::DeferredView);
    EXPECT_EQ(catalog.entries[0].extent.width, 1280u);
    EXPECT_EQ(catalog.entries[1].extent.width, 1280u);
    EXPECT_EQ(catalog.entries[2].extent.width, 256u);
    EXPECT_EQ(catalog.entries[3].extent.width, 256u);

    // The world View resizes: its entry is replaced, the thumbnail keeps its
    // own attachments and extent, and no third View appears.
    const auto resizedColor = std::make_shared<RenderTexture>();
    const auto resizedDepth = std::make_shared<RenderTexture>();
    DeferredRenderPipelineTestAccess::publishViewResources(
        pipeline,
        makeDeferredKey(11, Extent2D{.width = 640, .height = 480}),
        makeDeferredViews(resizedColor, resizedDepth, std::make_shared<RenderTexture>()));

    EXPECT_EQ(pipeline.buildDebugViews(11).viewportResources.colorOwner, resizedColor);
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), resizedDepth);
    EXPECT_EQ(pipeline.buildDebugViews(12).viewportResources.colorOwner, thumbColor);
    EXPECT_EQ(pipeline.getViewDepthImageShared(12), thumbDepth);

    RenderTargetCatalog resizedCatalog;
    pipeline.appendRenderTargetEntries(resizedCatalog);
    ASSERT_EQ(resizedCatalog.entries.size(), 5u);
    EXPECT_EQ(resizedCatalog.entries[0].extent.width, 640u);
    EXPECT_EQ(resizedCatalog.entries[1].extent.width, 640u);
    EXPECT_EQ(resizedCatalog.entries[2].extent.width, 256u);
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
    EXPECT_FALSE(ssao.inputDescriptorSet);
    EXPECT_EQ(ssao.viewId, 0u);

    ssao.frame  = RGBufferHandle{.index = 1, .generation = 2};
    ssao.albedo = RGTextureHandle{.index = 3, .generation = 4};
    EXPECT_TRUE(ssao.frame.isValid());
    EXPECT_TRUE(ssao.albedo.isValid());

    DeferredLightPassParams light{};
    EXPECT_FALSE(light.frame.handle.isValid());
    EXPECT_FALSE(light.light.handle.isValid());
    EXPECT_FALSE(light.gBufferDepth.isValid());
    EXPECT_FALSE(light.ssao.has_value());
    EXPECT_FALSE(light.viewColor.isValid());
    EXPECT_FALSE(light.gBufferTextureDescriptorSet);
    EXPECT_FALSE(light.shadowDescriptorSet);
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
    EXPECT_FALSE(skybox.viewColor.isValid());
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
    EXPECT_EQ(params.viewId, 0u);
    EXPECT_EQ(params.toneMap.input.set, DescriptorSetHandle{});
}

TEST(PostProcessingStageTest, BloomAndFinalizeViewIdsStayIndependent)
{
    BloomPostprocessing::RenderDesc bloom{};
    EXPECT_EQ(bloom.viewId, 0u);
    bloom.viewId = 11;

    PostProcessingStage::FinalizePassParams finalize{};
    finalize.viewId = 12;

    BasicPostprocessing::RenderDesc toneMap{};
    EXPECT_EQ(toneMap.viewId, 0u);
    EXPECT_EQ(toneMap.toneMap.input.set, DescriptorSetHandle{});
    toneMap.viewId = bloom.viewId;

    EXPECT_EQ(bloom.viewId, 11u);
    EXPECT_EQ(finalize.viewId, 12u);
    EXPECT_EQ(toneMap.viewId, 11u);
    EXPECT_NE(bloom.viewId, finalize.viewId);
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
        .viewExtent = {.width = 640, .height = 480},
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

TEST(ViewOverlayStageTest, BuildsSkyboxFrameDataWithoutCameraTranslation)
{
    ViewOverlayStage stage;

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

TEST_F(DeferredRenderPipelineSettingsTest, PersistentPostProcessAndDeferredExtrasSeedSnapshot)
{
    ConfigManager::get().set("runtime", "render.postprocess.bloom.enabled", true);
    ConfigManager::get().set("runtime", "render.postprocess.basic.tonemapping.exposure", 1.25f);
    ConfigManager::get().set("runtime", "render.deferred.ssaoEnabled", false);
    ConfigManager::get().set("runtime", "render.deferred.ssao.radius", 1.5f);
    ConfigManager::get().set("runtime", "render.deferred.light.enablePBRDiffuseIBL", false);
    ConfigManager::get().set("runtime", "render.deferred.light.enablePBRSpecularIBL", false);
    ConfigManager::get().set("runtime", "render.deferred.reverseViewportY", false);

    DeferredRenderPipeline pipeline;
    DeferredRenderPipelineTestAccess::loadPersistentSettings(pipeline);

    const auto snapshot = pipeline.buildSettingsSnapshot();
    EXPECT_TRUE(snapshot.postProcessing.bEnableBloom);
    EXPECT_FLOAT_EQ(snapshot.postProcessing.exposure, 1.25f);
    EXPECT_FALSE(snapshot.bSSAOEnabled);
    EXPECT_FLOAT_EQ(snapshot.ssaoRadius, 1.5f);
    EXPECT_FALSE(snapshot.bPBRDiffuseIBL);
    EXPECT_FALSE(snapshot.bPBRSpecularIBL);
    EXPECT_FALSE(snapshot.bReverseViewportY);
}

/// A Scene view task as the plan carries it: the declaration, whose identity is
/// what a tick says about a View existing.
SceneViewTask deferredPlanTask(SceneViewId viewId)
{
    SceneViewTask task;
    task.desc.viewId   = viewId;
    task.output.viewId = viewId;
    return task;
}

/// The deferred half of the batch's acceptance evidence: after a tick publishes
/// View A and View B, a tick that declares only B drops A's GBuffer, viewport and
/// postprocess owners, and leaves B exactly as it was.
TEST(DeferredRenderPipelineTest, AViewTheNextTickDoesNotDeclareIsEvictedAndTheOtherIsKept)
{
    DeferredRenderPipeline pipeline;

    auto worldColor    = std::make_shared<RenderTexture>();
    auto worldDepth    = std::make_shared<RenderTexture>();
    auto worldEntityId = std::make_shared<RenderTexture>();
    auto thumbColor    = std::make_shared<RenderTexture>();
    auto thumbDepth    = std::make_shared<RenderTexture>();
    auto thumbEntityId = std::make_shared<RenderTexture>();

    DeferredRenderPipelineTestAccess::publishViewResources(
        pipeline, makeDeferredKey(11, kWorldExtent), makeDeferredViews(worldColor, worldDepth, worldEntityId));
    DeferredRenderPipelineTestAccess::publishViewResources(
        pipeline, makeDeferredKey(12, kThumbnailExtent), makeDeferredViews(thumbColor, thumbDepth, thumbEntityId));

    // The table is the only owner left, so "the entry is gone" is provably also
    // "the attachments were released".
    std::weak_ptr<RenderTexture> thumbColorRef = thumbColor;
    std::weak_ptr<RenderTexture> thumbDepthRef = thumbDepth;
    thumbColor.reset();
    thumbDepth.reset();
    thumbEntityId.reset();

    SceneRenderPlan plan;
    plan.viewTasks.push_back(deferredPlanTask(11));

    DeferredRenderPipelineTestAccess::reconcilePublishedViews(pipeline, plan);

    // A is gone from the table, from the identity-carrying queries and from
    // memory.
    EXPECT_EQ(pipeline.buildDebugViews(12).viewportResources.colorOwner, nullptr);
    EXPECT_EQ(pipeline.getViewDepthImageShared(12), nullptr);
    EXPECT_EQ(pipeline.getEntityIdImageShared(12), nullptr);
    EXPECT_TRUE(thumbColorRef.expired());
    EXPECT_TRUE(thumbDepthRef.expired());

    // B is untouched.
    EXPECT_EQ(pipeline.buildDebugViews(11).viewportResources.colorOwner, worldColor);
    EXPECT_EQ(pipeline.buildDebugViews(11).viewportResources.depthOwner, worldDepth);
    EXPECT_EQ(pipeline.getViewDepthImageShared(11), worldDepth);
    EXPECT_EQ(pipeline.getEntityIdImageShared(11), worldEntityId);

    // The tick reconciles once and the criterion is the whole tick's
    // declarations, so repeating the call cannot drop a View the tick declares.
    DeferredRenderPipelineTestAccess::reconcilePublishedViews(pipeline, plan);
    EXPECT_EQ(pipeline.buildDebugViews(11).viewportResources.colorOwner, worldColor);

    // The panel's rows follow: one GBuffer row and one view row for the View
    // that is left, both naming it, plus the shadow map that belongs to no View.
    RenderTargetCatalog catalog;
    pipeline.appendRenderTargetEntries(catalog);
    ASSERT_EQ(catalog.entries.size(), 3u);
    EXPECT_EQ(catalog.entries[0].owner, RenderTargetCatalog::Entry::EOwner::DeferredGBuffer);
    EXPECT_EQ(catalog.entries[0].viewId, 11u);
    EXPECT_EQ(catalog.entries[1].owner, RenderTargetCatalog::Entry::EOwner::DeferredView);
    EXPECT_EQ(catalog.entries[1].viewId, 11u);
    EXPECT_EQ(catalog.entries[2].owner, RenderTargetCatalog::Entry::EOwner::DeferredShadow);
    EXPECT_EQ(catalog.entries[2].viewId, 0u);
}

} // namespace
} // namespace ya
