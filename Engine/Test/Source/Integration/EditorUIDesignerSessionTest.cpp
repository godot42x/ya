// Regression guards for EditorUIDesignerSession's direct-manipulation path: when a
// preview child lives under a canvas host, drag edits must read/write the
// parent-owned slot edge consistently across multiple drags.

#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Layout/UILayout.h"
#include "Scene/Core/SceneWidgetEntry.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

/// The designer session reads both tables from its layer, so a test needs a
/// headless layer. A document is an asset now: it is published into the store
/// under a path and opened by that path (no scene and no file involved).
struct FDesignerFixture
{
    EditorDocumentRegistry documents;
    UIDocumentStore        uiDocuments;
    EditorLayer            layer{nullptr};

    static constexpr std::string_view kDocumentPath = "Test/UI/Designer.yaui";

    FDesignerFixture() { layer.bindDocumentServices(&documents, &uiDocuments); }

    /// Publish `widget`'s subtree as the fixture document.
    void publish(const UIElement& widget)
    {
        uiDocuments.put(kDocumentPath, UIDocument::fromWidget(widget));
    }
};

} // namespace

TEST(EditorUIDesignerSessionTest, ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth)
{
    auto& registry = UITypeRegistry::instance();
    auto root = registry.createInstance(kTypeIdCanvasPanel);
    auto child = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    root->_name  = "Root";
    child->_name = "Child";
    root->addDetachedChild(child, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UICanvasSlot>();
        ASSERT_NE(slot, nullptr);
        FCanvasSlotArgs args;
        args.offset = {20.0f, 30.0f};
        args.fixedSize = {80.0f, 40.0f};
        slot->apply(args);
    });

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    const UIFrameSnapshot initial = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)initial;
    designer.selectByChildPath({0});

    UIElement* previewChild = designer.getSelectedWidget();
    ASSERT_NE(previewChild, nullptr);

    designer.beginResize(previewChild, {0.0f, 0.0f}, EditorUIDesignerSession::kResizeHandleRight);
    ASSERT_TRUE(designer.applyDragDelta({20.0f, 0.0f}));
    designer.endDrag();
    const UIFrameSnapshot afterFirstSnapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)afterFirstSnapshot;
    const Rect2D* afterFirst = designer.getSelectedLayoutRect();
    ASSERT_NE(afterFirst, nullptr);
    const glm::vec2 firstPos = afterFirst->pos;
    EXPECT_FLOAT_EQ(afterFirst->extent.x, 100.0f);

    designer.beginResize(previewChild, {0.0f, 0.0f}, EditorUIDesignerSession::kResizeHandleRight);
    ASSERT_TRUE(designer.applyDragDelta({10.0f, 0.0f}));
    designer.endDrag();
    const UIFrameSnapshot afterSecondSnapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)afterSecondSnapshot;
    const Rect2D* afterSecond = designer.getSelectedLayoutRect();
    ASSERT_NE(afterSecond, nullptr);
    EXPECT_EQ(afterSecond->pos, firstPos);
    EXPECT_FLOAT_EQ(afterSecond->extent.x, 110.0f);
    EXPECT_FLOAT_EQ(afterSecond->extent.y, 40.0f);
}

TEST(EditorUIDesignerSessionTest, FindByChildPathResolvesRootAndNestedWidgets)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  child    = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    root->_name  = "Root";
    child->_name = "Child";
    root->addDetachedChild(child);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewRoot = designer.findByChildPath({});
    ASSERT_NE(previewRoot, nullptr);
    EXPECT_EQ(previewRoot, designer.getPreviewRoot());
    EXPECT_EQ(previewRoot->_name, "Root");

    UIElement* previewChild = designer.findByChildPath({0});
    ASSERT_NE(previewChild, nullptr);
    EXPECT_EQ(previewChild->_name, "Child");
    EXPECT_EQ(designer.findByChildPath({1}), nullptr);
}

TEST(EditorUIDesignerSessionTest, ApplyWidgetDropReordersPreviewSiblings)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  first    = registry.createInstance(kTypeIdCanvasPanel);
    auto  second   = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    root->_name   = "Root";
    first->_name  = "First";
    second->_name = "Second";
    root->addDetachedChild(first);
    root->addDetachedChild(second);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);
    EXPECT_EQ(previewFirst->_name, "First");
    EXPECT_EQ(previewSecond->_name, "Second");

    designer.applyWidgetDrop(previewFirst, *previewSecond, EditorUIDesignerSession::EDropPos::After);

    UIElement* after0 = designer.findByChildPath({0});
    UIElement* after1 = designer.findByChildPath({1});
    ASSERT_NE(after0, nullptr);
    ASSERT_NE(after1, nullptr);
    EXPECT_EQ(after0->_name, "Second");
    EXPECT_EQ(after1->_name, "First");
}

TEST(EditorUIDesignerSessionTest, ApplyWidgetDropIntoNestsChild)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  first    = registry.createInstance(kTypeIdCanvasPanel);
    auto  second   = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    root->_name   = "Root";
    first->_name  = "First";
    second->_name = "Second";
    root->addDetachedChild(first);
    root->addDetachedChild(second);

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewRoot   = designer.getPreviewRoot();
    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewRoot, nullptr);
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);

    designer.applyWidgetDrop(previewSecond, *previewFirst, EditorUIDesignerSession::EDropPos::Into);

    EXPECT_EQ(previewRoot->getChildren().size(), 1u);
    UIElement* nestedParent = designer.findByChildPath({0});
    UIElement* nestedChild  = designer.findByChildPath({0, 0});
    ASSERT_NE(nestedParent, nullptr);
    ASSERT_NE(nestedChild, nullptr);
    EXPECT_EQ(nestedParent->_name, "First");
    EXPECT_EQ(nestedChild->_name, "Second");
}

TEST(EditorUIDesignerSessionTest, DocumentRegistryTracksDirtyCloseAndSingleton)
{
    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);

    designer.newDocument(kTypeIdCanvasPanel);
    ASSERT_TRUE(designer.hasDocument());
    ASSERT_NE(designer.documentSession(), nullptr);
    EXPECT_FALSE(designer.isDocumentDirty());
    EXPECT_TRUE(designer.documentSession()->ownsPreview());

    ASSERT_TRUE(designer.addPaletteWidget(kTypeIdCanvasPanel));
    EXPECT_TRUE(designer.isDocumentDirty());
    EXPECT_FALSE(designer.closeDocument());
    EXPECT_TRUE(designer.hasDocument());

    ASSERT_TRUE(designer.saveDocument());
    EXPECT_FALSE(designer.isDocumentDirty());
    EXPECT_TRUE(designer.closeDocument());
    EXPECT_FALSE(designer.hasDocument());
    EXPECT_EQ(fixture.documents.size(), 0u);
}

TEST(EditorUIDesignerSessionTest, OpenSceneEntrySharesDocumentSession)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    root->_name = "Root";

    FDesignerFixture fixture;
    fixture.publish(*root);

    SceneWidgetEntry entry;
    entry.entryId      = "hud";
    entry.documentPath = std::string(FDesignerFixture::kDocumentPath);

    EditorUIDesignerSession first(&fixture.layer);
    EditorUIDesignerSession second(&fixture.layer);
    first.openSceneEntry(entry);
    second.openSceneEntry(entry);

    ASSERT_NE(first.documentSession(), nullptr);
    EXPECT_EQ(first.documentSession(), second.documentSession());
    EXPECT_EQ(first.documentSession()->id(),
              makeEditorUIDocumentId(FDesignerFixture::kDocumentPath));
    EXPECT_TRUE(first.documentSession()->ownsPreview());
}

TEST(EditorUIDesignerSessionTest, CanvasPickingSelectsAButtonWithoutRunningItsClickHandler)
{
    // The designer is Authoring mode: a canvas click picks and manipulates, and
    // must never reach a widget's runtime click handler. That handler belongs to
    // the runtime instance (GameUIHost), which is a different tree for the same
    // document.
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  button   = registry.createInstance(kTypeIdButton);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(button, nullptr);
    root->_name   = "Root";
    button->_name = "Button";

    int clicks = 0;
    auto* typed = dynamic_cast<UIButton*>(button.get());
    ASSERT_NE(typed, nullptr);
    typed->_onClick = [&clicks]() { ++clicks; };

    root->addDetachedChild(button, [](UIElement&, UISlot& edge) {
        auto* slot = edge.as<UICanvasSlot>();
        ASSERT_NE(slot, nullptr);
        FCanvasSlotArgs args;
        args.fixedSize        = {120.0f, 40.0f};
        args.widthSizeMode    = EWidgetSizeMode::Fixed;
        args.heightSizeMode   = EWidgetSizeMode::Fixed;
        args.offset           = {40.0f, 40.0f};
        ASSERT_TRUE(slot->applyArgs(args));
    });

    FDesignerFixture fixture;
    fixture.publish(*root);

    EditorUIDesignerSession designer(&fixture.layer);
    designer.openDocument(FDesignerFixture::kDocumentPath);

    UIElement* previewRoot = designer.getPreviewRoot();
    ASSERT_NE(previewRoot, nullptr);
    ASSERT_EQ(previewRoot->getChildren().size(), 1u);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    // Picking finds the button and selects it -- geometry still works.
    const Rect2D&   buttonRect = previewRoot->getChildren()[0]->_layoutRect;
    const glm::vec2 inside     = buttonRect.pos + buttonRect.extent * 0.5f;
    UIElement*      picked     = designer.pickAt(inside);
    ASSERT_NE(picked, nullptr);
    designer.select(picked);
    EXPECT_EQ(designer.getSelectedWidget(), previewRoot->getChildren()[0].get());

    // Selecting is not clicking: the preview has no dispatch path, so a canvas
    // interaction cannot become a runtime interaction.
    EXPECT_EQ(clicks, 0);
}

} // namespace ya
