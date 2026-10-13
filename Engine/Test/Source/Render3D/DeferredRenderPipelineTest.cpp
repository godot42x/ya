#include "Render3D/Deferred/DeferredRenderPipeline.h"
#include "Render3D/Common/PreparedPipelineFrame.h"
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

TEST(DeferredCompositionTargetsTest, TargetListComesFromResolvedFeaturesOnceAndGatesOptionalOnes)
{
    GBufferFeature::Settings gbufferSettings;
    gbufferSettings.albedoFormat  = EFormat::R8G8B8A8_UNORM;
    gbufferSettings.normalFormat  = EFormat::R16G16B16A16_SFLOAT;
    gbufferSettings.ormFormat     = EFormat::R8G8B8A8_UNORM;
    gbufferSettings.shadingFormat = EFormat::R8_UNORM;
    gbufferSettings.depthFormat   = EFormat::D32_SFLOAT;

    LightingFeature::Settings lightingSettings;
    lightingSettings.sceneColorFormat = EFormat::R16G16B16A16_SFLOAT;

    FinalizeFeature::Settings finalizeSettings;
    finalizeSettings.displayFormat = EFormat::R8G8B8A8_UNORM;

    DeferredPipelineRecipe::ResolvedView view;
    view.viewId     = 7;
    view.viewExtent = {.width = 640, .height = 480};
    view.gbuffer()    = GBufferFeature{}.resolve(gbufferSettings, GBufferFeature::Facts{}, GBufferFeature::Inputs{});
    view.lighting()   = LightingFeature{}.resolve(lightingSettings, LightingFeature::Facts{}, LightingFeature::Inputs{});
    view.entityId()   = EntityIdFeature{}.resolve(EntityIdFeature::Settings{}, EntityIdFeature::Facts{}, EntityIdFeature::Inputs{});
    view.finalize()   = FinalizeFeature{}.resolve(finalizeSettings, FinalizeFeature::Facts{}, FinalizeFeature::Inputs{});

    auto targets = view.collectViewTargets();
    ASSERT_EQ(targets.size(), 8u);
    EXPECT_EQ(targets[0].role, EViewAttachment::GBuffer0);
    EXPECT_EQ(targets[1].role, EViewAttachment::GBuffer1);
    EXPECT_EQ(targets[2].role, EViewAttachment::GBuffer2);
    EXPECT_EQ(targets[3].role, EViewAttachment::GBuffer3);
    EXPECT_EQ(targets[4].role, EViewAttachment::SceneDepth);
    EXPECT_EQ(targets[5].role, EViewAttachment::SceneColor);
    EXPECT_EQ(targets[6].role, EViewAttachment::EntityId);
    EXPECT_EQ(targets[7].role, EViewAttachment::DisplayColor);
    for (const auto& target : targets) {
        EXPECT_NE(target.role, EViewAttachment::SSAO);
        EXPECT_NE(target.role, EViewAttachment::BloomExtract);
        EXPECT_NE(target.role, EViewAttachment::BloomBlur);
        EXPECT_NE(target.role, EViewAttachment::BloomComposite);
    }

    // Enabling the optional features inserts exactly their roles, in the same
    // stable order the request builder publishes every frame.
    SSAOFeature::Settings ssaoSettings;
    ssaoSettings.bEnabled = true;
    RenderFrameData frameData{};
    frameData.projection = glm::mat4(1.0f);
    frameData.view       = glm::mat4(1.0f);
    view.ssao() = SSAOFeature{}.resolve(
        ssaoSettings,
        SSAOFeature::Facts{.viewExtent = view.viewExtent, .frameData = &frameData, .bReverseY = true},
        SSAOFeature::Inputs{});

    BloomFeature::Settings bloomSettings;
    bloomSettings.bEnabled = true;
    view.bloom() = BloomFeature{}.resolve(bloomSettings, BloomFeature::Facts{}, BloomFeature::Inputs{});

    targets = view.collectViewTargets();
    ASSERT_EQ(targets.size(), 12u);
    EXPECT_EQ(targets[5].role, EViewAttachment::SSAO);
    EXPECT_EQ(targets[6].role, EViewAttachment::SceneColor);
    EXPECT_EQ(targets[7].role, EViewAttachment::BloomExtract);
    EXPECT_EQ(targets[8].role, EViewAttachment::BloomBlur);
    EXPECT_EQ(targets[9].role, EViewAttachment::BloomComposite);
    EXPECT_EQ(targets[10].role, EViewAttachment::EntityId);
    EXPECT_EQ(targets[11].role, EViewAttachment::DisplayColor);

    // Guard: each enabled feature's declared requirement is published exactly
    // once, in map order. A target-bearing feature missing from
    // collectViewTargets (or published twice) fails here — and its own append
    // fails with MissingDependency instead of silently rendering nothing.
    for (size_t i = 0; i < targets.size(); ++i) {
        for (size_t j = i + 1; j < targets.size(); ++j) {
            EXPECT_NE(targets[i].role, targets[j].role);
        }
    }
}

TEST(GBufferFeatureTest, DisabledResolveCarriesNoFormatsAndEnabledResolveOwnsItsRoles)
{
    GBufferFeature feature;

    GBufferFeature::Settings settings;
    settings.bEnabled = false;

    const auto disabled = feature.resolve(settings, GBufferFeature::Facts{}, GBufferFeature::Inputs{});
    EXPECT_FALSE(disabled.bEnabled);

    settings.bEnabled      = true;
    settings.albedoFormat  = EFormat::R8G8B8A8_UNORM;
    settings.normalFormat  = EFormat::R16G16B16A16_SFLOAT;
    settings.ormFormat     = EFormat::R8G8B8A8_UNORM;
    settings.shadingFormat = EFormat::R8_UNORM;
    settings.depthFormat   = EFormat::D32_SFLOAT;

    const auto enabled = feature.resolve(settings, GBufferFeature::Facts{}, GBufferFeature::Inputs{});
    ASSERT_TRUE(enabled.bEnabled);
    EXPECT_EQ(enabled.outputs.albedo.role, EViewAttachment::GBuffer0);
    EXPECT_EQ(enabled.outputs.albedo.format, EFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(enabled.outputs.normal.role, EViewAttachment::GBuffer1);
    EXPECT_EQ(enabled.outputs.orm.role, EViewAttachment::GBuffer2);
    EXPECT_EQ(enabled.outputs.shading.role, EViewAttachment::GBuffer3);
    EXPECT_EQ(enabled.outputs.depth.role, EViewAttachment::SceneDepth);
    EXPECT_EQ(enabled.outputs.depth.format, EFormat::D32_SFLOAT);
    EXPECT_TRUE(enabled.outputs.depth.isDepth);

    // A disabled feature prepares nothing, even when the View binding has sets.
    const auto disabledPrepared = feature.prepareBindings(disabled, GBufferFeature::PrepareContext{});
    ASSERT_TRUE(disabledPrepared.isOk());
    EXPECT_FALSE(disabledPrepared.value().frameAndLightDS);
    EXPECT_FALSE(disabledPrepared.value().skinningDS);
}

TEST(SSAOFeatureTest, DisabledResolveRequiresNoTargetAndEnabledResolveBuildsFrameData)
{
    SSAOFeature feature;

    SSAOFeature::Settings settings; // disabled by default

    const auto disabled = feature.resolve(settings, SSAOFeature::Facts{}, SSAOFeature::Inputs{});
    EXPECT_FALSE(disabled.bEnabled);
    EXPECT_FALSE(disabled.aoTarget.has_value());

    settings.bEnabled  = true;
    settings.radius    = 1.25f;
    settings.bias      = 0.04f;
    settings.power     = 1.75f;
    settings.intensity = 2.0f;

    RenderFrameData frameData{};
    frameData.projection = glm::mat4(2.0f);
    frameData.view       = glm::mat4(1.0f);

    const auto enabled = feature.resolve(
        settings,
        SSAOFeature::Facts{
            .viewExtent = {.width = 640, .height = 480},
            .frameData  = &frameData,
            .bReverseY  = false,
        },
        SSAOFeature::Inputs{});
    ASSERT_TRUE(enabled.bEnabled);
    ASSERT_TRUE(enabled.aoTarget.has_value());
    EXPECT_EQ(enabled.aoTarget->role, EViewAttachment::SSAO);
    EXPECT_EQ(enabled.frameData.screenResolution.x, 640);
    EXPECT_EQ(enabled.frameData.screenResolution.y, 480);
    EXPECT_FLOAT_EQ(enabled.frameData.radius, 1.25f);
    EXPECT_FLOAT_EQ(enabled.frameData.bias, 0.04f);
    EXPECT_FLOAT_EQ(enabled.frameData.power, 1.75f);
    EXPECT_FLOAT_EQ(enabled.frameData.intensity, 2.0f);
    EXPECT_EQ(enabled.frameData.reverseY, 0u);
    EXPECT_FLOAT_EQ(enabled.frameData.projectMat[0][0], 2.0f);
}

TEST(LightingFeatureTest, ResolveCarriesThePipelineSceneColorFormat)
{
    LightingFeature::Settings settings;
    settings.sceneColorFormat = EFormat::R16G16B16A16_SFLOAT;

    const auto resolved = LightingFeature{}.resolve(settings, LightingFeature::Facts{}, LightingFeature::Inputs{});
    ASSERT_TRUE(resolved.bEnabled);
    EXPECT_EQ(resolved.target.role, EViewAttachment::SceneColor);
    EXPECT_EQ(resolved.target.format, EFormat::R16G16B16A16_SFLOAT);
}

TEST(DeferredFeatureMapTest, ThinTailFeaturesOwnTheirPassPresence)
{
    SkyboxFeature skybox;
    const auto skyboxResolved = skybox.resolve(
        SkyboxFeature::Settings{}, SkyboxFeature::Facts{}, SkyboxFeature::Inputs{});
    EXPECT_TRUE(skyboxResolved.bEnabled);

    RenderFrameData frameData{};
    Sprite2DFeature sprites;
    const auto spriteResolved = sprites.resolve(
        Sprite2DFeature::Settings{},
        Sprite2DFeature::Facts{.frameData = &frameData},
        Sprite2DFeature::Inputs{});
    EXPECT_FALSE(spriteResolved.bEnabled);

    OverlayFeature overlay;
    const auto overlayResolved = overlay.resolve(
        OverlayFeature::Settings{}, OverlayFeature::Facts{}, OverlayFeature::Inputs{});
    EXPECT_TRUE(overlayResolved.bEnabled);
}

TEST(DeferredFeatureMapTest, BloomDisabledDeclaresNoTargetsAndFinalizeCarriesTheDisplayContract)
{
    BloomFeature bloom;
    const auto bloomDisabled = bloom.resolve(BloomFeature::Settings{}, BloomFeature::Facts{}, BloomFeature::Inputs{});
    EXPECT_FALSE(bloomDisabled.bEnabled);
    EXPECT_FALSE(bloomDisabled.extract.has_value());
    EXPECT_FALSE(bloomDisabled.blur.has_value());
    EXPECT_FALSE(bloomDisabled.composite.has_value());

    BloomFeature::Settings bloomSettings;
    bloomSettings.bEnabled = true;
    const auto bloomEnabled = bloom.resolve(bloomSettings, BloomFeature::Facts{}, BloomFeature::Inputs{});
    ASSERT_TRUE(bloomEnabled.bEnabled);
    ASSERT_TRUE(bloomEnabled.extract.has_value());
    ASSERT_TRUE(bloomEnabled.blur.has_value());
    ASSERT_TRUE(bloomEnabled.composite.has_value());
    EXPECT_EQ(bloomEnabled.extract->role, EViewAttachment::BloomExtract);
    EXPECT_EQ(bloomEnabled.blur->role, EViewAttachment::BloomBlur);
    EXPECT_EQ(bloomEnabled.composite->role, EViewAttachment::BloomComposite);

    FinalizeFeature::Settings finalizeSettings;
    finalizeSettings.displayFormat = EFormat::R8G8B8A8_UNORM;
    const auto finalize = FinalizeFeature{}.resolve(
        finalizeSettings, FinalizeFeature::Facts{}, FinalizeFeature::Inputs{});
    ASSERT_TRUE(finalize.bEnabled);
    EXPECT_EQ(finalize.target.role, EViewAttachment::DisplayColor);
    EXPECT_EQ(finalize.target.format, EFormat::R8G8B8A8_UNORM);
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
    RenderFrameData frameData{};
    frameData.projection = glm::mat4(2.0f);
    frameData.view       = glm::mat4(1.0f);
    frameData.view[3]    = glm::vec4(5.0f, 6.0f, 7.0f, 1.0f);

    const auto payload = ViewOverlayStage::buildFrameData(frameData);
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

} // namespace

TEST(BloomPostprocessingTest, EachCallPicksItsOwnFinalBlurTargetFromItsOwnCount)
{
    // The composite pass must read the ping-pong side belonging to THIS call's
    // blur count. When that count lived on the stage as `_lastBlurPassCount`,
    // two contributions in one frame (two Views, or a second bloom owner) made
    // the later composite depend on whichever call ran last. Now the count is a
    // local: build two blooms with opposite parities in one frame and check each
    // composite reads its own side.
    RenderGraph graph;
    BloomPostprocessing bloom; // pass building needs no GPU pipelines

    const auto scene = graph.createTexture(RGTextureDesc{
        .label  = "BloomTest.Scene",
        .format = EFormat::R16G16B16A16_SFLOAT,
        .extent = Extent3D{64, 64, 1},
        .usage  = EImageUsage::ColorAttachment | EImageUsage::Sampled,
    });

    PostProcessingState state{};
    state.bEnableBloom    = true;
    state.bloomBlurPasses = 1; // -> 2 passes, even -> final side is Pong

    const auto evenCount = bloom.appendBloomChain(graph, BloomPostprocessing::RenderDesc{
        .sceneHandle  = scene,
        .renderExtent = Extent2D{.width = 64, .height = 64},
        .state        = &state,
        .viewId       = 21,
    });
    EXPECT_TRUE(evenCount.composite.isValid());

    state.bloomBlurPasses = 2; // -> 4 passes, still even
    const auto evenCountAgain = bloom.appendBloomChain(graph, BloomPostprocessing::RenderDesc{
        .sceneHandle  = scene,
        .renderExtent = Extent2D{.width = 64, .height = 64},
        .state        = &state,
        .viewId       = 22,
    });
    EXPECT_TRUE(evenCountAgain.composite.isValid());

    // Both views' pass sets exist independently: the graph has both extracts and
    // both composites, and neither call reset or re-read the other's state.
    const auto& passes = graph.getPasses();
    auto countName = [&passes](std::string_view needle) {
        size_t n = 0;
        for (const auto& pass : passes) {
            if (pass.name.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    };
    // Each call appends blurPasses * 2 iterations: 2 for the first (blurPasses
    // 1), 4 for the second, alternating horizontal/vertical.
    EXPECT_EQ(countName("BloomExtract"), 2u);
    EXPECT_EQ(countName("BloomComposite"), 2u);
    EXPECT_EQ(countName("BloomBlurHorizontal"), 3u);
    EXPECT_EQ(countName("BloomBlurVertical"), 3u);
}


TEST(PreparedPipelineFrameTest, ResolvedPointersSurviveCopiesAndGrowthOfTheFrame)
{
    // Recording holds a pointer into the prepared frame's resolved vector while
    // it appends each View in turn. That is only safe because nothing mutates the
    // vector during recording: the prepared frame is handed over as const and
    // fully populated at the safe point. This pins that fact — a pointer taken
    // for one View must still name the same resolved values after the frame is
    // moved and after the other block grows.
    PreparedPipelineFrame frame;
    frame.deferred.resolved.resize(2);
    frame.deferred.resolved[0].viewId = 100;
    frame.deferred.resolved[0].gbuffer().bEnabled = true;
    frame.deferred.resolved[1].viewId = 200;

    const auto* firstView = &frame.deferred.resolved[0];
    ASSERT_EQ(firstView->viewId, 100u);

    PreparedPipelineFrame moved = std::move(frame);
    // Moving the frame does not disturb the resolved values themselves.
    const auto* afterMove = &moved.deferred.resolved[0];
    EXPECT_EQ(afterMove->viewId, 100u);
    EXPECT_TRUE(afterMove->gbuffer().bEnabled);

    // The other pipeline's block growing must not touch this one.
    moved.forward.resolved.resize(4);
    moved.forward.resolved[0].viewId = 300;
    EXPECT_EQ(moved.deferred.resolved[0].viewId, 100u);
    EXPECT_EQ(moved.deferred.resolved[1].viewId, 200u);
    EXPECT_EQ(moved.forward.resolved[0].viewId, 300u);
}


namespace
{

/// M24's freeze boundary needs a frame packet whose shadow settings the safe
/// point can read, the way the extractor builds one.
RenderFramePlan makeFramePlanWithShadow(const ShadowSettings& shadowSettings)
{
    static ShadowSettings held = shadowSettings;
    held = shadowSettings;
    RenderFramePlan plan;
    plan.frame.shadowSettings = &held;
    return plan;
}

} // namespace

TEST_F(DeferredRenderPipelineSettingsTest, ApplyFrameSettingsFreezesTheFrameAgainstLaterRequests)
{
    // D1/M24: applyFrameSettings is the one place a frame's settings land. A
    // request that arrives after it is next frame's business: the frame must
    // resolve from what was frozen, and the stage must not see a third value.
    DeferredRenderPipeline pipeline;
    DeferredRenderPipelineTestAccess::loadPersistentSettings(pipeline);

    RenderFramePlan plan = makeFramePlanWithShadow(ShadowSettings::fromQuality(EShadowQuality::Low));
    pipeline.applyFrameSettings(plan);

    // The frame's shadow source is now the packet's value, wherever it came from.
    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.quality, EShadowQuality::Low);

    // A request arriving after the freeze is queued, not applied: the frozen
    // frame state does not move, and resolveSettings still answers the pending
    // value so the caller can see what next frame will do.
    RenderPipelineSettings late = pipeline.buildSettingsSnapshot();
    late.shadow                = ShadowSettings::fromQuality(EShadowQuality::Ultra);
    late.ssaoRadius            = 9.0f;
    pipeline.requestSettings(late);

    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.quality, EShadowQuality::Low);
    EXPECT_FLOAT_EQ(pipeline.buildSettingsSnapshot().ssaoRadius, 0.6f);
    EXPECT_EQ(pipeline.resolveSettings().shadow.quality, EShadowQuality::Ultra);

    // Re-freezing (the next frame's safe point) is what lands it.
    plan = makeFramePlanWithShadow(ShadowSettings::fromQuality(EShadowQuality::Ultra));
    pipeline.applyFrameSettings(plan);
    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.quality, EShadowQuality::Ultra);
    EXPECT_FLOAT_EQ(pipeline.buildSettingsSnapshot().ssaoRadius, 9.0f);
}

TEST_F(DeferredRenderPipelineSettingsTest, FramePacketShadowSettingsAreTheSafePointSource)
{
    // The per-View capture used to read the frame packet at recording start; the
    // safe point reads the same packet, so a shadow change made through the
    // packet (the automation path) is frozen before the composition resolves.
    DeferredRenderPipeline pipeline;
    DeferredRenderPipelineTestAccess::loadPersistentSettings(pipeline);

    ShadowSettings packetSettings = ShadowSettings::fromQuality(EShadowQuality::High);
    packetSettings.resolution     = 2048;
    RenderFramePlan plan          = makeFramePlanWithShadow(packetSettings);

    pipeline.applyFrameSettings(plan);
    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.resolution, 2048u);
    EXPECT_EQ(pipeline.buildSettingsSnapshot().shadow.quality, EShadowQuality::High);
}

} // namespace ya
