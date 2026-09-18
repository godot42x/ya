#include "GameRuntime/Lifecycle/HostSceneExtract.h"

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
#include <utility>
#include <vector>

namespace ya
{
namespace
{

SceneViewDesc makeViewDesc(Scene& scene, SceneViewId viewId, const glm::vec3& cameraPos)
{
    const glm::mat4 view       = glm::translate(glm::mat4(1.0f), -cameraPos);
    const glm::mat4 projection = glm::perspective(1.0f, 1.5f, 0.1f, 100.0f);
    return SceneViewDesc{
        .scene        = &scene,
        .viewId       = viewId,
        .view         = view,
        .projection   = projection,
        .cameraPos    = cameraPos,
        .viewportRect = {.pos = {0.0f, 0.0f}, .extent = {64.0f, 36.0f}},
    };
}

bool declareAll(SceneRenderScheduler& scheduler, std::span<const SceneViewDesc> views)
{
    bool bDeclared = false;
    for (const SceneViewDesc& view : views) {
        bDeclared = scheduler.submit(view) || bDeclared;
    }
    return bDeclared;
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

TEST(HostSceneExtractTest, DualLiveScenesExtractIsolatedSnapshots)
{
    Scene sceneA("Authoring");
    Scene sceneB("Play");
    ASSERT_NE(sceneA.getInstanceId(), sceneB.getInstanceId());
    addDirectionalLight(sceneA, glm::vec3(1.0f, 0.0f, 0.0f), 2.0f);
    addDirectionalLight(sceneB, glm::vec3(0.0f, 0.0f, 1.0f), 4.0f);

    // Declarations are written once, as the owner means them, and handed to the
    // scheduler unchanged.
    const SceneViewDesc views[] = {
        makeViewDesc(sceneA, 11, glm::vec3(1.0f, 0.0f, 0.0f)),
        makeViewDesc(sceneB, 21, glm::vec3(0.0f, 2.0f, 0.0f)),
    };

    SceneRenderScheduler scheduler;
    scheduler.beginTick(31);
    ASSERT_TRUE(declareAll(scheduler, views));
    SceneRenderPlan sealed = scheduler.seal();

    // seal() only groups: the snapshot table exists, its content does not.
    ASSERT_EQ(sealed.snapshots.size(), 2u);
    EXPECT_FALSE(sealed.snapshots[0].snapshot);
    EXPECT_FALSE(sealed.snapshots[1].snapshot);
    // The plan holds the declaration verbatim, so the camera is already here.
    EXPECT_EQ(sealed.viewportTasks[0].desc.view, views[0].view);
    EXPECT_EQ(sealed.viewportTasks[0].desc.cameraPos, views[0].cameraPos);

    const ExtractedSceneRender extracted = extractHostSceneSnapshots(std::move(sealed), nullptr);
    const SceneRenderPlan&      plan      = extracted.plan();

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

    // Each plan entry carries the Scene its declaration named, so nothing has
    // to map an entry back to the declaration list to find it again.
    EXPECT_EQ(plan.viewportTasks[0].desc.scene, &sceneA);
    EXPECT_EQ(plan.viewportTasks[1].desc.scene, &sceneB);
    EXPECT_EQ(plan.snapshots[plan.viewportTasks[0].snapshotIndex].scene, &sceneA);
    EXPECT_EQ(plan.snapshots[plan.viewportTasks[1].snapshotIndex].scene, &sceneB);

    RenderFrameData frameA;
    RenderFrameData frameB;
    RenderFrameExtractor::prepareView(
        RenderFrameExtractor::ViewPrepareInput{
            .view           = plan.viewportTasks[0].desc.view,
            .projection     = plan.viewportTasks[0].desc.projection,
            .viewProjection = plan.viewportTasks[0].desc.viewProjection(),
            .cameraPos      = plan.viewportTasks[0].desc.cameraPos,
            .viewportExtent = Extent2D::fromVec2(plan.viewportTasks[0].desc.viewportRect.extent),
        },
        snapshotA,
        frameA);
    RenderFrameExtractor::prepareView(
        RenderFrameExtractor::ViewPrepareInput{
            .view           = plan.viewportTasks[1].desc.view,
            .projection     = plan.viewportTasks[1].desc.projection,
            .viewProjection = plan.viewportTasks[1].desc.viewProjection(),
            .cameraPos      = plan.viewportTasks[1].desc.cameraPos,
            .viewportExtent = Extent2D::fromVec2(plan.viewportTasks[1].desc.viewportRect.extent),
        },
        snapshotB,
        frameB);
    EXPECT_EQ(frameA.sceneSnapshot.get(), snapshotA.get());
    EXPECT_EQ(frameB.sceneSnapshot.get(), snapshotB.get());
    EXPECT_NE(frameA.sceneSnapshot.get(), frameB.sceneSnapshot.get());
}

TEST(HostSceneExtractTest, SameLiveSceneTwoViewsShareSnapshot)
{
    Scene scene("World");
    addDirectionalLight(scene, glm::vec3(1.0f, 1.0f, 1.0f), 1.0f);

    const SceneViewDesc views[] = {
        makeViewDesc(scene, 11, glm::vec3(1.0f, 0.0f, 0.0f)),
        makeViewDesc(scene, 12, glm::vec3(0.0f, 4.0f, 0.0f)),
    };

    SceneRenderScheduler scheduler;
    scheduler.beginTick(32);
    ASSERT_TRUE(declareAll(scheduler, views));
    const ExtractedSceneRender extracted = extractHostSceneSnapshots(scheduler.seal(), nullptr);
    const SceneRenderPlan&      plan      = extracted.plan();

    ASSERT_EQ(plan.viewFamilies.size(), 1u);
    ASSERT_EQ(plan.snapshots.size(), 1u);
    ASSERT_EQ(plan.viewportTasks.size(), 2u);
    EXPECT_EQ(plan.snapshotFor(plan.viewportTasks[0]), plan.snapshotFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.familyFor(plan.viewportTasks[0]), plan.familyFor(plan.viewportTasks[1]));
    EXPECT_EQ(plan.viewportTasks[0].desc.scene, &scene);
    EXPECT_EQ(plan.viewportTasks[1].desc.scene, &scene);
    // One extracted snapshot, two Views: the camera is what differs.
    EXPECT_NE(plan.viewportTasks[0].desc.view, plan.viewportTasks[1].desc.view);
}

TEST(HostSceneExtractTest, DeclaringOutsideATickIsRejected)
{
    Scene scene("Idle");
    const SceneViewDesc views[] = {makeViewDesc(scene, 11, glm::vec3(0.0f))};
    SceneRenderScheduler scheduler;
    EXPECT_FALSE(declareAll(scheduler, views));
}

TEST(HostSceneExtractTest, DeclaringWithoutAWholePixelIsRejected)
{
    Scene scene("Authoring");

    // A View's textures are sized from its rect. Sub-pixel geometry -- what an
    // unlaid-out or collapsed panel reports, and what uninitialized geometry
    // looks like -- truncates to nothing, so it is not a View at all.
    SceneViewDesc subPixel = makeViewDesc(scene, 11, glm::vec3(0.0f));
    subPixel.viewportRect.extent = {0.4f, 0.4f};

    SceneViewDesc denormal = makeViewDesc(scene, 12, glm::vec3(0.0f));
    denormal.viewportRect.extent = {1.4e-43f, 1.4e-45f};

    SceneRenderScheduler scheduler;
    scheduler.beginTick(5);
    EXPECT_FALSE(scheduler.submit(subPixel));
    EXPECT_FALSE(scheduler.submit(denormal));

    // One pixel is enough, so a genuinely tiny panel still renders.
    SceneViewDesc onePixel = makeViewDesc(scene, 13, glm::vec3(0.0f));
    onePixel.viewportRect.extent = {1.2f, 1.2f};
    EXPECT_TRUE(scheduler.submit(onePixel));

    ASSERT_EQ(scheduler.seal().viewportTasks.size(), 1u);
}

} // namespace ya
