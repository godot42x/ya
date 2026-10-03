#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorViewProducer.h"
#include "GameEditor/UI/Shell/EditorSurface.h"
#include "GameEditor/UI/Tabs/EditorViewportTab.h"

#include "GameRuntime/Render/RuntimeGameViewProducer.h"

#include "ECS/Systems/Components/CameraComponent.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"
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

/// The camera preview, when one is declared: a non-display-root View whose
/// image the viewport chrome samples.
const SceneViewDesc* findNonDisplayRoot(const SceneViewCollector& collector)
{
    for (const SceneViewDesc& view : collector.views()) {
        if (!view.bDisplayRoot) {
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
    // A viewport is on screen; the producer declares its views only then.
    layer.addViewportShown();

    Scene scene("Authoring");
    layer.notifyViewportWidgetRect(Rect2D{.pos = {12.0f, 24.0f}, .extent = {960.0f, 540.0f}}, {});

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, producer.authoringKey().viewId());
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->scene, &scene);
    // The editor's own panel geometry is what it declares, not the host rect the
    // context carries: the panel is a fact only the editor has.
    EXPECT_FLOAT_EQ(primary->outputRect.extent.x, 960.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.y, 540.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.pos.x, 12.0f);
    // Generated companions are editor furniture and the authoring viewport is
    // what shows the scene while authoring, so it draws them without asking.
    EXPECT_TRUE(drawsGizmos(*primary));
    // No camera is selected, so there is no camera preview inset to declare.
    EXPECT_EQ(findNonDisplayRoot(collector), nullptr);
    EXPECT_FLOAT_EQ(primary->pixelDensity, 1.0f);
}

TEST(EditorViewProducerTest, AuthoringViewportExtentIsLogicalTimesPixelDensity)
{
    App                app;
    EditorLayer        layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);
    layer.addViewportShown();
    layer.setViewportPixelDensity(2.0f);

    Scene scene("Authoring");
    layer.notifyViewportWidgetRect(Rect2D{.pos = {12.0f, 24.0f}, .extent = {349.0f, 197.0f}}, {});

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, producer.authoringKey().viewId());
    ASSERT_NE(primary, nullptr);
    EXPECT_FLOAT_EQ(primary->outputRect.pos.x, 12.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.pos.y, 24.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.x, 698.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.y, 394.0f);
    EXPECT_FLOAT_EQ(primary->pixelDensity, 2.0f);
}

TEST(EditorViewProducerTest, AuthoringViewportFallsBackToItsDefaultSizeBeforeLayout)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);
    layer.addViewportShown();

    Scene scene("Authoring");

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    // No layout has run yet, so the editor's own default panel size stands in
    // and the view is never declared with an empty rect.
    const SceneViewDesc* primary = findView(collector, producer.authoringKey().viewId());
    ASSERT_NE(primary, nullptr);
    const Rect2D declared = layer.getViewportRect();
    EXPECT_GT(declared.extent.x, 0.0f);
    EXPECT_GT(declared.extent.y, 0.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.x, declared.extent.x);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.y, declared.extent.y);
}

TEST(EditorViewProducerTest, PanelGeometryTooSmallForAPixelDoesNotBecomeTheViewRect)
{
    App             app;
    EditorLayer     layer(&app);
    EditorViewProducer producer;
    producer.bind(app, layer);
    layer.addViewportShown();

    // A collapsed panel, or geometry that was never written, reports extents
    // that pass a plain "greater than zero" test and then truncate to a
    // zero-sized View -- which cannot be sized into render targets.
    layer.notifyViewportWidgetRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {0.4f, 1.4e-43f}}, {});

    Scene              scene("Authoring");
    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);

    const SceneViewDesc* primary = findView(collector, producer.authoringKey().viewId());
    ASSERT_NE(primary, nullptr);
    const Extent2D declaredPixels = Extent2D::fromVec2(primary->outputRect.extent);
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
    layer.addViewportShown();

    Scene scene("Authoring");
    Node3D* cameraNode = scene.createNode3D("PreviewCamera");
    ASSERT_NE(cameraNode, nullptr);
    ASSERT_NE(cameraNode->getEntity(), nullptr);
    ASSERT_NE(cameraNode->getEntity()->addComponent<CameraComponent>(), nullptr);
    layer.setSelectedEntity(cameraNode->getEntity());

    SceneViewCollector collector;
    producer.collectSceneViews(makeEditorContext(scene), collector);
    const SceneViewDesc* preview = findNonDisplayRoot(collector);
    ASSERT_NE(preview, nullptr);
    // A camera preview shows what that camera sees: generated editor
    // companions are authoring furniture and never appear in it, whatever the
    // editor's gizmo option says.
    EXPECT_FALSE(drawsGizmos(*preview));

    layer.setEditorGizmoShown(true);

    SceneViewCollector withOption;
    producer.collectSceneViews(makeEditorContext(scene), withOption);
    const SceneViewDesc* previewWithOption = findNonDisplayRoot(withOption);
    ASSERT_NE(previewWithOption, nullptr);
    EXPECT_FALSE(drawsGizmos(*previewWithOption));

    // The option lives on the editor, which is what the View menu and the
    // automation call both drive, and the authoring viewport is the view it
    // reaches.
    EXPECT_TRUE(layer.isEditorGizmoShown());
    const SceneViewDesc* primaryWithOption = findView(withOption, producer.authoringKey().viewId());
    ASSERT_NE(primaryWithOption, nullptr);
    EXPECT_TRUE(drawsGizmos(*primaryWithOption));
}

TEST(EditorViewProducerTest, HidingTheViewportDeclaresNoEditorViewAtAll)
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
    layer.notifyViewportWidgetRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {960.0f, 540.0f}}, {});

    layer.addViewportShown();
    SceneViewCollector shown;
    producer.collectSceneViews(makeEditorContext(scene), shown);
    ASSERT_NE(findView(shown, producer.authoringKey().viewId()), nullptr);
    ASSERT_NE(findNonDisplayRoot(shown), nullptr);

    // Selecting another tab in the viewport's stack -- or the level editor tab
    // that owns that stack -- detaches the widget. There is then no image to draw
    // into, so the editor declares no View and the tick records no world graph at
    // all, rather than recording one nothing samples.
    layer.removeViewportShown();
    SceneViewCollector hidden;
    producer.collectSceneViews(makeEditorContext(scene), hidden);
    EXPECT_TRUE(hidden.views().empty());

    // Coming back the declared geometry is the panel's, and neither the stale
    // last-laid-out rect nor the default size is involved.
    layer.notifyViewportWidgetRect(Rect2D{.pos = {4.0f, 8.0f}, .extent = {800.0f, 450.0f}}, {});
    layer.addViewportShown();
    SceneViewCollector reshown;
    producer.collectSceneViews(makeEditorContext(scene), reshown);
    const SceneViewDesc* primary = findView(reshown, producer.authoringKey().viewId());
    ASSERT_NE(primary, nullptr);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.x, 800.0f);
    EXPECT_FLOAT_EQ(primary->outputRect.extent.y, 450.0f);
}

TEST(EditorViewProducerTest, AHiddenViewportStopsCapturingViewportInput)
{
    App         app;
    EditorLayer layer(&app);

    layer.addViewportShown();
    layer.notifyViewportWidgetRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {960.0f, 540.0f}}, {});
    layer.setViewportHoverFocus(true, true);
    ASSERT_TRUE(layer.shouldCaptureInput());

    // The widget left the tree, so nothing can hover or focus it. A hover flag
    // left standing would keep feeding the editor camera input meant for the tab
    // that replaced the viewport.
    layer.removeViewportShown();
    EXPECT_FALSE(layer.isViewportHovered());
    EXPECT_FALSE(layer.isViewportFocused());
    EXPECT_FALSE(layer.shouldCaptureInput());
}

/// The point of owner-scoped identity: the game's world viewport and the
/// editor's authoring viewport are two different Views that happen to be the
/// only one shown at a time. Under one global id space they had to take turns on
/// slot 1, so "which View is this" was answerable only by reading the declarer;
/// now each owner names its own primary and the ids cannot collide.
TEST(EditorViewProducerTest, EditorAndGameViewportsAreDistinctIdentities)
{
    App                 app;
    EditorLayer         layer(&app);
    EditorViewProducer  editor;
    RuntimeGameViewProducer game;
    editor.bind(app, layer);
    game.bind(app);

    const SceneViewId editorAuthoring = editor.authoringKey().viewId();
    const SceneViewId editorPreview   = editor.previewKey().viewId();
    const SceneViewId gameViewport    = game.displayRootKey().viewId();

    // Every View this build can show has its own identity...
    EXPECT_NE(editorAuthoring, 0u);
    EXPECT_NE(editorPreview, 0u);
    EXPECT_NE(gameViewport, 0u);
    EXPECT_NE(editorAuthoring, editorPreview);
    EXPECT_NE(editorAuthoring, gameViewport);
    EXPECT_NE(editorPreview, gameViewport);

    // ...and a key's owner half is what keeps them apart, not the local half.
    EXPECT_EQ(editor.authoringKey().owner, editor.previewKey().owner);
    EXPECT_NE(editor.authoringKey().owner, game.displayRootKey().owner);

    // An unnamed owner is not a View at all: it reads as the same 0 the output
    // tables already treat as "nothing published".
    EXPECT_FALSE((SceneViewKey{.owner = 0, .local = 1}.valid()));
    EXPECT_FALSE((SceneViewKey{.owner = 1, .local = 0}.valid()));
    EXPECT_EQ((SceneViewKey{.owner = 0, .local = 1}.viewId()), 0u);
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

/// The viewport tab registers itself with the shell on attach and clears on
/// detach. This case drives that production edge end to end -- dock detach ->
/// sink -> layer -> producer -- because the wiring, not the flag, is what makes
/// a hidden viewport cost nothing.
TEST(EditorViewProducerTest, DockTabDetachIsWhatHidesTheViewport)
{
    App           app;
    EditorLayer   layer(&app);
    EditorSurface surface;
    surface.bind(layer);

    WidgetTree tree({.width = 400, .height = 300});
    auto root = std::make_shared<UICanvasPanel>("Root");
    FCanvasSlotArgs rootSlot;
    rootSlot.anchorMin = {0.0f, 0.0f};
    rootSlot.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, rootSlot).valid());

    // Before the tab is in the tree there is no viewport, so nothing is declared.
    EditorViewProducer producer;
    producer.bind(app, layer);
    Scene              scene("Authoring");
    SceneViewCollector beforeAttach;
    producer.collectSceneViews(makeEditorContext(scene), beforeAttach);
    ASSERT_TRUE(beforeAttach.views().empty());

    auto tab = std::make_shared<EditorViewportTab>(&surface);
    ASSERT_TRUE(tree.attach(*root, tab).valid());
    EXPECT_TRUE(layer.isViewportShown());

    SceneViewCollector attached;
    producer.collectSceneViews(makeEditorContext(scene), attached);
    EXPECT_NE(findView(attached, producer.authoringKey().viewId()), nullptr);

    // The dock detaches the widget when another tab in the stack is selected.
    tree.detach(*tab);
    EXPECT_FALSE(layer.isViewportShown());

    SceneViewCollector detached;
    producer.collectSceneViews(makeEditorContext(scene), detached);
    EXPECT_TRUE(detached.views().empty());
}

/// A second editor window has its own chrome and its own viewport, and the
/// layer tracks the whole editor's authoring view. One window switching tabs
/// must not stop a viewport the other window is still showing.
TEST(EditorViewProducerTest, OneWindowHidingItsViewportDoesNotHideTheOtherWindows)
{
    App           app;
    EditorLayer   layer(&app);
    EditorSurface firstWindow;
    EditorSurface secondWindow;
    firstWindow.bind(layer);
    secondWindow.bind(layer);

    WidgetTree firstTree({.width = 400, .height = 300});
    WidgetTree secondTree({.width = 400, .height = 300});
    auto       attachViewport = [](WidgetTree& source, EditorSurface& sink) {
        auto root = std::make_shared<UICanvasPanel>("Root");
        FCanvasSlotArgs rootSlot;
        rootSlot.anchorMin = {0.0f, 0.0f};
        rootSlot.anchorMax = {1.0f, 1.0f};
        EXPECT_TRUE(source.attach(*source.getLayer(WidgetTree::ELayer::Content), root, rootSlot).valid());
        auto tab = std::make_shared<EditorViewportTab>(&sink);
        EXPECT_TRUE(source.attach(*root, tab).valid());
        return tab;
    };

    auto firstTab  = attachViewport(firstTree, firstWindow);
    auto secondTab = attachViewport(secondTree, secondWindow);
    EXPECT_TRUE(layer.isViewportShown());

    firstTree.detach(*firstTab);
    EXPECT_TRUE(layer.isViewportShown());

    EditorViewProducer producer;
    producer.bind(app, layer);
    Scene              scene("Authoring");
    SceneViewCollector stillShown;
    producer.collectSceneViews(makeEditorContext(scene), stillShown);
    EXPECT_NE(findView(stillShown, producer.authoringKey().viewId()), nullptr);

    secondTree.detach(*secondTab);
    EXPECT_FALSE(layer.isViewportShown());
}

} // namespace ya
