#include "GameRuntime/Lifecycle/HostSceneRenderSubmit.h"

#include "Core/Log.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "GameRuntime/Utility/RenderFrameExtractor.h"
#include "Render3D/Common/SceneRenderScheduler.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace ya
{
namespace
{

HostSceneViewSubmit makeView(Scene& scene, SceneViewId viewId, const glm::vec3& cameraPos)
{
    const glm::mat4 view       = glm::translate(glm::mat4(1.0f), -cameraPos);
    const glm::mat4 projection = glm::perspective(1.0f, 1.5f, 0.1f, 100.0f);
    return HostSceneViewSubmit{
        .scene        = &scene,
        .viewId       = viewId,
        .view         = view,
        .projection   = projection,
        .cameraPos    = cameraPos,
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {64.0f, 36.0f}},
    };
}

void addDirectionalLight(Scene& scene, const glm::vec3& color, float intensity)
{
    Node3D* node = scene.createNode3D("Light");
    YA_CORE_ASSERT(node && node->getEntity(), "Test scene node must be valid");
    auto* light = node->getEntity()->addComponent<DirectionalLightComponent>();
    YA_CORE_ASSERT(light, "DirectionalLightComponent must attach");
    light->_color    = color;
    light->intensity = intensity;
}

} // namespace

TEST(HostSceneRenderSubmitTest, DualLiveScenesExtractIsolatedSnapshots)
{
    Scene sceneA("Authoring");
    Scene sceneB("Play");
    ASSERT_NE(sceneA.getInstanceId(), sceneB.getInstanceId());
    addDirectionalLight(sceneA, glm::vec3(1.0f, 0.0f, 0.0f), 2.0f);
    addDirectionalLight(sceneB, glm::vec3(0.0f, 0.0f, 1.0f), 4.0f);

    const HostSceneViewSubmit views[] = {
        makeView(sceneA, 11, glm::vec3(1.0f, 0.0f, 0.0f)),
        makeView(sceneB, 21, glm::vec3(0.0f, 2.0f, 0.0f)),
    };

    SceneRenderScheduler scheduler;
    scheduler.beginTick(31);
    ASSERT_TRUE(submitHostSceneViews(scheduler, nullptr, views));
    const SceneRenderPlan plan = scheduler.seal();

    ASSERT_EQ(plan.viewFamilies.size(), 2u);
    ASSERT_EQ(plan.snapshots.size(), 2u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_NE(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_NE(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));

    const auto snapshotA = plan.snapshotFor(plan.viewportTasks[0]);
    const auto snapshotB = plan.snapshotFor(plan.viewportTasks[1]);
    ASSERT_TRUE(snapshotA);
    ASSERT_TRUE(snapshotB);
    EXPECT_TRUE(snapshotA->bHasDirectionalLight);
    EXPECT_TRUE(snapshotB->bHasDirectionalLight);
    EXPECT_NE(snapshotA->directionalLightSource.color, snapshotB->directionalLightSource.color);
    EXPECT_NE(snapshotA->directionalLightSource.intensity, snapshotB->directionalLightSource.intensity);

    EXPECT_EQ(derivedSceneForHostView(views, plan.viewportTasks[0]), &sceneA);
    EXPECT_EQ(derivedSceneForHostView(views, plan.viewportTasks[1]), &sceneB);

    RenderFrameData frameA;
    RenderFrameData frameB;
    RenderFrameExtractor::prepareView(
        RenderFrameExtractor::ViewPrepareInput{
            .view           = plan.viewportTasks[0].view,
            .projection     = plan.viewportTasks[0].projection,
            .viewProjection = plan.viewportTasks[0].viewProjection,
            .cameraPos      = plan.viewportTasks[0].cameraPos,
            .viewportExtent = Extent2D::fromVec2(plan.viewportTasks[0].viewportRect.extent),
        },
        snapshotA,
        frameA);
    RenderFrameExtractor::prepareView(
        RenderFrameExtractor::ViewPrepareInput{
            .view           = plan.viewportTasks[1].view,
            .projection     = plan.viewportTasks[1].projection,
            .viewProjection = plan.viewportTasks[1].viewProjection,
            .cameraPos      = plan.viewportTasks[1].cameraPos,
            .viewportExtent = Extent2D::fromVec2(plan.viewportTasks[1].viewportRect.extent),
        },
        snapshotB,
        frameB);
    EXPECT_EQ(frameA.sceneSnapshot.get(), snapshotA.get());
    EXPECT_EQ(frameB.sceneSnapshot.get(), snapshotB.get());
    EXPECT_NE(frameA.sceneSnapshot.get(), frameB.sceneSnapshot.get());
}

TEST(HostSceneRenderSubmitTest, SameLiveSceneTwoViewsShareSnapshot)
{
    Scene scene("World");
    addDirectionalLight(scene, glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);

    const HostSceneViewSubmit views[] = {
        makeView(scene, 11, glm::vec3(1.0f, 0.0f, 0.0f)),
        makeView(scene, 12, glm::vec3(0.0f, 4.0f, 0.0f)),
    };

    SceneRenderScheduler scheduler;
    scheduler.beginTick(32);
    ASSERT_TRUE(submitHostSceneViews(scheduler, nullptr, views));
    const SceneRenderPlan plan = scheduler.seal();

    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    ASSERT_EQ(plan.snapshots.size(), 1u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
    EXPECT_EQ(derivedSceneForHostView(views, plan.viewportTasks[0]), &scene);
    EXPECT_EQ(derivedSceneForHostView(views, plan.viewportTasks[1]), &scene);
}

TEST(HostSceneRenderSubmitTest, ClosedSchedulerRejectsSubmit)
{
    Scene scene("Idle");
    const HostSceneViewSubmit views[] = {makeView(scene, 11, glm::vec3(0.0f))};
    SceneRenderScheduler scheduler;
    EXPECT_FALSE(submitHostSceneViews(scheduler, nullptr, views));
}

} // namespace ya
