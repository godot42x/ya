#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorViewProducer.h"

#include "ECS/Systems/Components/CameraComponent.h"
#include "GameRuntime/App.h"
#include "Render3D/Common/RenderFeatures.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

SceneViewCollectContext makeEditorContext(Scene& scene)
{
    return SceneViewCollectContext{
        .activeScene    = &scene,
        .viewportRect   = {.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}},
        .viewportExtent = {.width = 1280u, .height = 720u},
        .hostTick       = 7,
        .deltaTime      = 1.0f / 60.0f,
    };
}

const SceneViewDesc* findView(const SceneViewCollector& collector, SceneViewId viewId)
{
    for (const SceneViewDesc& view : collector.views()) {
        if (view.viewId == viewId) {
            return &view;
        }
    }
    return nullptr;
}

/// The inset a camera preview declares: it composes onto the authoring view
/// instead of owning the host viewport.
const SceneViewDesc* findComposedInset(const SceneViewCollector& collector)
{
    for (const SceneViewDesc& view : collector.views()) {
        if (view.composeOntoViewId != 0) {
            return &view;
        }
    }
    return nullptr;
}

bool drawsGizmos(const SceneViewDesc& view)
{
    return rendersFeature(toMask(ERenderFeature::Gizmo), view.features);
}

} // namespace

TEST(EditorViewProducerTest, AuthoringViewportDrawsEditorFurnitureWhileAuthoring)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);

    Scene scene("Authoring");

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, kPrimarySceneViewId);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->scene, &scene);
    // Generated companions are editor furniture and the authoring viewport is
    // what shows the scene while authoring, so it draws them without asking.
    EXPECT_TRUE(drawsGizmos(*primary));
    // No camera is selected, so there is no camera preview inset to declare.
    EXPECT_EQ(findComposedInset(collector), nullptr);
}

TEST(EditorViewProducerTest, GizmoViewOptionOnlyReachesViewsThatAskForIt)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);

    Scene scene("Authoring");
    Node3D* cameraNode = scene.createNode3D("PreviewCamera");
    ASSERT_NE(cameraNode, nullptr);
    ASSERT_NE(cameraNode->getEntity(), nullptr);
    ASSERT_NE(cameraNode->getEntity()->addComponent<CameraComponent>(), nullptr);
    layer.setSelectedEntity(cameraNode->getEntity());

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);
    const SceneViewDesc* preview = findComposedInset(collector);
    ASSERT_NE(preview, nullptr);
    // A camera preview shows what that camera sees, so editor furniture stays
    // out of it until the editor asks for it.
    EXPECT_FALSE(drawsGizmos(*preview));

    layer.setEditorGizmoShown(true);

    SceneViewCollector withOption;
    producer.collectSceneViews(makeEditorContext(scene), withOption);
    const SceneViewDesc* previewWithOption = findComposedInset(withOption);
    ASSERT_NE(previewWithOption, nullptr);
    EXPECT_TRUE(drawsGizmos(*previewWithOption));

    // The option lives on the editor, which is what the View menu and the
    // automation call both drive; the authoring viewport keeps drawing its
    // furniture either way.
    EXPECT_TRUE(layer.isEditorGizmoShown());
    const SceneViewDesc* primaryWithOption = findView(withOption, kPrimarySceneViewId);
    ASSERT_NE(primaryWithOption, nullptr);
    EXPECT_TRUE(drawsGizmos(*primaryWithOption));
}

} // namespace ya
