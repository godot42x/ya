#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"
#include "Render3D/ResourceResolveProbe.h"
#include "Render3D/Services/GameplayResourceBinding.h"
#include "Render3D/Terrain/TerrainProcessor.h"
#include "RHI/Core/OffscreenJob.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

#include <vector>

namespace ya
{
namespace
{

/// Seed walks a fixed set of views. A later prepare, and a scene with more
/// entities, must not walk them again: discovery is the seed plus the edit
/// funnel, and completions re-enter through callbacks.
uint64_t steadyPrepareTouches(int entityCount)
{
    resourceResolveComponentTouches() = 0;

    Scene scene("steady");
    auto& registry = scene.getRegistry();
    for (int i = 0; i < entityCount; ++i) {
        const auto entity = registry.create();
        registry.emplace<SkyboxComponent>(entity);
        registry.emplace<EnvironmentLightingComponent>(entity);
        registry.emplace<TerrainComponent>(entity);
        if (i == 0) {
            auto& mesh = registry.emplace<StaticMeshComponent>(entity);
            mesh.setPrimitiveGeometry(EPrimitiveGeometry::None);
        }
    }

    EnvironmentLightingProcessor lighting;
    TerrainProcessor             terrain;
    GameplayResourceBinding      binding;
    const std::vector<Scene*>    scenes{&scene};
    const auto                   prepare = [&] {
        lighting.prepareScenes(scenes, 0.0f);
        terrain.prepareScenes(scenes, 0.0f);
        binding.prepareScenes(scenes, 0.0f);
    };

    prepare();
    const uint64_t seeded = resourceResolveComponentTouches();
    prepare();
    EXPECT_EQ(resourceResolveComponentTouches(), seeded);
    return seeded;
}

TEST(ResourceResolveSteadyState, SecondPrepareTouchesNoComponentViewAndIgnoresEntityCount)
{
    const uint64_t one  = steadyPrepareTouches(1);
    const uint64_t many = steadyPrepareTouches(64);
    EXPECT_GT(one, 0u);
    EXPECT_EQ(one, many);
}

TEST(OffscreenJobFinished, FinalizeNotifiesGpuCompletedAndFailed)
{
    auto completed = std::make_shared<OffscreenJobState>();
    completed->phase = EOffscreenJobPhase::Recorded;
    int completedCalls = 0;
    completed->onFinished = [&] { ++completedCalls; };

    auto failed = std::make_shared<OffscreenJobState>();
    failed->phase = EOffscreenJobPhase::Failed;
    int failedCalls = 0;
    failed->onFinished = [&] { ++failedCalls; };

    auto pending = std::make_shared<OffscreenJobState>();
    pending->phase = EOffscreenJobPhase::Pending;
    int pendingCalls = 0;
    pending->onFinished = [&] { ++pendingCalls; };

    std::vector<std::shared_ptr<OffscreenJobState>> jobs{completed, failed, pending};
    finalizeSubmittedOffscreenJobs(jobs);

    EXPECT_TRUE(jobs.empty());
    EXPECT_EQ(completed->phase, EOffscreenJobPhase::GpuCompleted);
    EXPECT_EQ(completedCalls, 1);
    EXPECT_EQ(failedCalls, 1);
    EXPECT_EQ(pendingCalls, 0);
}

} // namespace
} // namespace ya
