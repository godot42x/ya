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
        .renderResolution = {.width = 1280u, .height = 720u},
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
    layer.notifyViewportWidgetRect(Rect2D{.pos = {12.0f, 24.0f}, .extent = {960.0f, 540.0f}}, {});

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, kPrimarySceneViewId);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->scene, &scene);
    // The editor's own panel geometry is what it declares, not the host rect the
    // context carries: the panel is a fact only the editor has.
    EXPECT_FLOAT_EQ(primary->viewportRect.extent.x, 960.0f);
    EXPECT_FLOAT_EQ(primary->viewportRect.extent.y, 540.0f);
    EXPECT_FLOAT_EQ(primary->viewportRect.pos.x, 12.0f);
    // Generated companions are editor furniture and the authoring viewport is
    // what shows the scene while authoring, so it draws them without asking.
    EXPECT_TRUE(drawsGizmos(*primary));
    // No camera is selected, so there is no camera preview inset to declare.
    EXPECT_EQ(findComposedInset(collector), nullptr);
}

TEST(EditorViewProducerTest, AuthoringViewportFallsBackToItsDefaultSizeBeforeLayout)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);

    Scene scene("Authoring");

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    // No layout has run yet, so the editor's own default panel size stands in
    // and the view is never declared with an empty rect.
    const SceneViewDesc* primary = findView(collector, kPrimarySceneViewId);
    ASSERT_NE(primary, nullptr);
    const Rect2D declared = layer.getViewportRect();
    EXPECT_GT(declared.extent.x, 0.0f);
    EXPECT_GT(declared.extent.y, 0.0f);
    EXPECT_FLOAT_EQ(primary->viewportRect.extent.x, declared.extent.x);
    EXPECT_FLOAT_EQ(primary->viewportRect.extent.y, declared.extent.y);
}

TEST(EditorViewProducerTest, PanelGeometryTooSmallForAPixelDoesNotBecomeTheViewRect)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);

    // A collapsed panel, or geometry that was never written, reports extents
    // that pass a plain "greater than zero" test and then truncate to a
    // zero-sized View -- which cannot be sized into render targets.
    layer.notifyViewportWidgetRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {0.4f, 1.4e-43f}}, {});

    Scene              scene("Authoring");
    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, kPrimarySceneViewId);
    ASSERT_NE(primary, nullptr);
    const Extent2D declaredPixels = Extent2D::fromVec2(primary->viewportRect.extent);
    EXPECT_GT(declaredPixels.width, 0u);
    EXPECT_GT(declaredPixels.height, 0u);
    EXPECT_TRUE(layer.describesPixels(layer.getViewportRect()));
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
    // A camera preview shows what that camera sees: generated editor
    // companions are authoring furniture and never appear in it, whatever the
    // editor's gizmo option says.
    EXPECT_FALSE(drawsGizmos(*preview));

    layer.setEditorGizmoShown(true);

    SceneViewCollector withOption;
    producer.collectSceneViews(makeEditorContext(scene), withOption);
    const SceneViewDesc* previewWithOption = findComposedInset(withOption);
    ASSERT_NE(previewWithOption, nullptr);
    EXPECT_FALSE(drawsGizmos(*previewWithOption));

    // The option lives on the editor, which is what the View menu and the
    // automation call both drive, and the authoring viewport is the view it
    // reaches.
    EXPECT_TRUE(layer.isEditorGizmoShown());
    const SceneViewDesc* primaryWithOption = findView(withOption, kPrimarySceneViewId);
    ASSERT_NE(primaryWithOption, nullptr);
    EXPECT_TRUE(drawsGizmos(*primaryWithOption));
}

TEST(EditorViewProducerTest, PreviewPanelKeepsWorldInputOutOfThePanel)
{
    App         app;
    EditorLayer layer(&app);

    const Rect2D world{.pos = {100.0f, 50.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D panel{.pos = {760.0f, 500.0f}, .extent = {120.0f, 90.0f}};
    layer.notifyViewportWidgetRect(world, panel);

    glm::vec2 local{};
    // The world image still maps into viewport-local coordinates.
    ASSERT_TRUE(layer.screenToViewport(200.0f, 100.0f, local.x, local.y));
    EXPECT_FLOAT_EQ(local.x, 100.0f);
    EXPECT_FLOAT_EQ(local.y, 50.0f);
    ASSERT_TRUE(layer.screenToViewport(200.0f, 300.0f, local.x, local.y));

    // Chrome stacked on the image owns its own area: a point on the preview
    // panel is not a world point, so it cannot pick what the world shows under
    // the panel.
    EXPECT_FALSE(layer.screenToViewport(800.0f, 540.0f, local.x, local.y));

    // With no preview panel there is nothing to exclude.
    layer.notifyViewportWidgetRect(world, {});
    EXPECT_TRUE(layer.screenToViewport(800.0f, 540.0f, local.x, local.y));
}

} // namespace ya
