#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/ResourceResolveProbe.h"
#include "RHI/Core/Image.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Texture.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <vector>

namespace ya
{
namespace
{

struct NullImage final : IImage
{
    ImageHandle     getHandle() const override { return {}; }
    uint32_t        getWidth() const override { return 4; }
    uint32_t        getHeight() const override { return 4; }
    EFormat::T      getFormat() const override { return EFormat::R8G8B8A8_UNORM; }
    uint32_t        getMipLevels() const override { return 1; }
    uint32_t        getArrayLayers() const override { return 1; }
    EImageUsage::T  getUsage() const override { return EImageUsage::Sampled; }
    EImageLayout::T getCompatibilityLayout() const override { return EImageLayout::ShaderReadOnlyOptimal; }
    void            setDebugName(const std::string&) override {}
};

struct NullImageView final : IImageView
{
    ImageViewHandle getHandle() const override { return {}; }
    void            setDebugName(const std::string&) override {}
};

std::shared_ptr<Texture> makeCubemap(const char* label)
{
    return Texture::wrap(std::make_shared<NullImage>(), std::make_shared<NullImageView>(), label);
}

std::shared_ptr<RenderTexture> makeRenderTexture(const char* label)
{
    return RenderTexture::wrap(std::make_shared<NullImage>(), std::make_shared<NullImageView>(), label);
}

SkyboxRuntimeState readySkybox(const std::shared_ptr<Texture>& cubemap)
{
    SkyboxRuntimeState state;
    state.resolveState   = ESkyboxResolveState::Ready;
    state.cubemapTexture = cubemap;
    return state;
}

EnvironmentLightingRuntimeState readyEnvironment(const std::shared_ptr<Texture>& cubemap)
{
    EnvironmentLightingRuntimeState state;
    state.sourceState    = EEnvironmentLightingSourceResolveState::Ready;
    state.cubemapTexture = cubemap;
    state.irradianceState = EEnvironmentLightingIrradianceResolveState::Disabled;
    state.prefilterState  = EEnvironmentLightingPrefilterResolveState::Disabled;
    return state;
}

TEST(EnvironmentLightingSelection, SteadyConsumerDoesNotWalkComponentViews)
{
    resourceResolveComponentTouches() = 0;

    Scene scene("EnvSelectionSteady");
    auto& registry = scene.getRegistry();
    for (int i = 0; i < 8; ++i) {
        const auto entity = registry.create();
        registry.emplace<SkyboxComponent>(entity);
        registry.emplace<EnvironmentLightingComponent>(entity);
    }

    EnvironmentLightingProcessor lighting;
    const std::vector<Scene*>    scenes{&scene};
    lighting.prepareScenes(scenes, 0.0f);
    lighting.prepareScenes(scenes, 0.0f);
    const uint64_t seeded = resourceResolveComponentTouches();

    for (int i = 0; i < 32; ++i) {
        (void)lighting.resolveSceneEnvironmentLightingResources(&scene);
        (void)lighting.resolveSceneSkyboxResource(&scene);
        (void)lighting.findFirstSceneSkyboxState(&scene);
        (void)lighting.findFirstSceneEnvironmentLightingState(&scene);
    }
    EXPECT_EQ(resourceResolveComponentTouches(), seeded);
}

TEST(EnvironmentLightingSelection, FirstReadyHoldsUntilItStopsThenTheNextTakesOver)
{
    entt::registry registry;
    // Lower index becomes ready second, so a min-index scan would pick it.
    const auto low  = registry.create();
    const auto high = registry.create();
    ASSERT_LT(entt::to_entity(low), entt::to_entity(high));

    const auto highCubemap = makeCubemap("high");
    const auto lowCubemap  = makeCubemap("low");

    EnvironmentLightingProcessor::SceneWork work;
    work.skyboxStates.emplace(high, readySkybox(highCubemap));
    work.noteSkyboxContribution(high);
    work.skyboxStates.emplace(low, readySkybox(lowCubemap));
    work.noteSkyboxContribution(low);

    const auto* selected = work.findFirstReadySkyboxState();
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->cubemapTexture, highCubemap);

    work.skyboxStates[high].resolveState = ESkyboxResolveState::Failed;
    work.noteSkyboxContribution(high);
    selected = work.findFirstReadySkyboxState();
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->cubemapTexture, lowCubemap);

    work.skyboxStates.erase(low);
    work.noteSkyboxContribution(low);
    EXPECT_EQ(work.findFirstReadySkyboxState(), nullptr);
}

TEST(EnvironmentLightingSelection, EnvironmentChannelsKeepTheFirstContributor)
{
    entt::registry registry;
    const auto first  = registry.create();
    const auto second = registry.create();

    const auto firstCubemap  = makeCubemap("env-first");
    const auto secondCubemap = makeCubemap("env-second");
    const auto firstIrradiance  = makeRenderTexture("irr-first");
    const auto secondIrradiance = makeRenderTexture("irr-second");
    const auto secondPrefilter  = makeRenderTexture("pre-second");

    EnvironmentLightingProcessor::SceneWork work;
    auto firstState = readyEnvironment(firstCubemap);
    firstState.irradianceState       = EEnvironmentLightingIrradianceResolveState::Ready;
    firstState.irradianceRenderImage = firstIrradiance;
    work.environmentStates.emplace(first, firstState);
    work.noteEnvironmentContribution(first);

    auto secondState = readyEnvironment(secondCubemap);
    secondState.irradianceState       = EEnvironmentLightingIrradianceResolveState::Ready;
    secondState.irradianceRenderImage = secondIrradiance;
    secondState.prefilterState        = EEnvironmentLightingPrefilterResolveState::Ready;
    secondState.prefilterRenderImage  = secondPrefilter;
    work.environmentStates.emplace(second, secondState);
    work.noteEnvironmentContribution(second);

    const auto held = work.resolveSelectedResources();
    EXPECT_EQ(held.cubemap, firstCubemap->getResourceShared());
    EXPECT_EQ(held.irradiance, firstIrradiance->getResourceShared());
    EXPECT_EQ(held.prefilter, secondPrefilter->getResourceShared());

    work.environmentStates[first].sourceState = EEnvironmentLightingSourceResolveState::Failed;
    work.environmentStates[first].irradianceState = EEnvironmentLightingIrradianceResolveState::Failed;
    work.noteEnvironmentContribution(first);

    const auto advanced = work.resolveSelectedResources();
    EXPECT_EQ(advanced.cubemap, secondCubemap->getResourceShared());
    EXPECT_EQ(advanced.irradiance, secondIrradiance->getResourceShared());
    EXPECT_EQ(advanced.prefilter, secondPrefilter->getResourceShared());

    work.environmentStates.erase(second);
    work.noteEnvironmentContribution(second);
    const auto cleared = work.resolveSelectedResources();
    EXPECT_EQ(cleared.cubemap, nullptr);
    EXPECT_EQ(cleared.irradiance, nullptr);
    EXPECT_EQ(cleared.prefilter, nullptr);
    EXPECT_EQ(work.findFirstReadyEnvironmentLightingState(), nullptr);
}

TEST(EnvironmentLightingSelection, SceneSkyboxStaysSelectedUntilThatSourceStops)
{
    entt::registry registry;
    const auto skyboxEntity = registry.create();
    const auto alias        = registry.create();
    const auto ownCubemap   = registry.create();

    const auto skyboxTexture = makeCubemap("scene-skybox");
    const auto ownTexture    = makeCubemap("own-cubemap");

    EnvironmentLightingProcessor::SceneWork work;
    work.skyboxStates.emplace(skyboxEntity, readySkybox(skyboxTexture));
    work.noteSkyboxContribution(skyboxEntity);

    EnvironmentLightingRuntimeState aliasState;
    aliasState.sourceState      = EEnvironmentLightingSourceResolveState::Ready;
    aliasState.bUsesSceneSkybox = true;
    aliasState.irradianceState  = EEnvironmentLightingIrradianceResolveState::Disabled;
    aliasState.prefilterState   = EEnvironmentLightingPrefilterResolveState::Disabled;
    work.sceneSkyboxEnvironmentDependents.insert(alias);
    work.environmentStates.emplace(alias, aliasState);
    work.noteEnvironmentContribution(alias);

    work.environmentStates.emplace(ownCubemap, readyEnvironment(ownTexture));
    work.noteEnvironmentContribution(ownCubemap);

    EXPECT_EQ(work.resolveSelectedResources().cubemap, skyboxTexture->getResourceShared());

    work.skyboxStates.erase(skyboxEntity);
    work.noteSkyboxContribution(skyboxEntity);
    EXPECT_EQ(work.resolveSelectedResources().cubemap, ownTexture->getResourceShared());

    work.environmentStates.erase(ownCubemap);
    work.noteEnvironmentContribution(ownCubemap);
    EXPECT_EQ(work.resolveSelectedResources().cubemap, nullptr);
}

} // namespace
} // namespace ya
