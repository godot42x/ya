// Regression guards for UIDesignerPanel's direct-manipulation path: when a
// preview child lives under a canvas host, drag edits must read/write the
// parent-owned slot edge consistently across multiple drags.

#include "GameEditor/Panels/UIDesignerPanel.h"
#include "GameEditor/UI/EditorDocumentSession.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Layout/UILayout.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(UIDesignerPanelTest, ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth)
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

    auto document = UIDocument::fromWidget(*root);
    ASSERT_NE(document, nullptr);

    UIDesignerPanel designer(nullptr);
    designer.openDocument(document);
    const UIFrameSnapshot initial = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)initial;
    designer.selectByChildPath({0});

    UIElement* previewChild = designer.getSelectedWidget();
    ASSERT_NE(previewChild, nullptr);

    designer.beginResize(previewChild, {0.0f, 0.0f}, UIDesignerPanel::kResizeHandleRight);
    ASSERT_TRUE(designer.applyDragDelta({20.0f, 0.0f}));
    designer.endDrag();
    const UIFrameSnapshot afterFirstSnapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    (void)afterFirstSnapshot;
    const Rect2D* afterFirst = designer.getSelectedLayoutRect();
    ASSERT_NE(afterFirst, nullptr);
    const glm::vec2 firstPos = afterFirst->pos;
    EXPECT_FLOAT_EQ(afterFirst->extent.x, 100.0f);

    designer.beginResize(previewChild, {0.0f, 0.0f}, UIDesignerPanel::kResizeHandleRight);
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

TEST(UIDesignerPanelTest, FindByChildPathResolvesRootAndNestedWidgets)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    auto  child    = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    ASSERT_NE(child, nullptr);
    root->_name  = "Root";
    child->_name = "Child";
    root->addDetachedChild(child);

    auto document = UIDocument::fromWidget(*root);
    ASSERT_NE(document, nullptr);

    UIDesignerPanel designer(nullptr);
    designer.openDocument(document);
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

TEST(UIDesignerPanelTest, ApplyWidgetDropReordersPreviewSiblings)
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

    auto document = UIDocument::fromWidget(*root);
    ASSERT_NE(document, nullptr);

    UIDesignerPanel designer(nullptr);
    designer.openDocument(document);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);
    EXPECT_EQ(previewFirst->_name, "First");
    EXPECT_EQ(previewSecond->_name, "Second");

    designer.applyWidgetDrop(previewFirst, *previewSecond, UIDesignerPanel::EDropPos::After);

    UIElement* after0 = designer.findByChildPath({0});
    UIElement* after1 = designer.findByChildPath({1});
    ASSERT_NE(after0, nullptr);
    ASSERT_NE(after1, nullptr);
    EXPECT_EQ(after0->_name, "Second");
    EXPECT_EQ(after1->_name, "First");
}

TEST(UIDesignerPanelTest, ApplyWidgetDropIntoNestsChild)
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

    auto document = UIDocument::fromWidget(*root);
    ASSERT_NE(document, nullptr);

    UIDesignerPanel designer(nullptr);
    designer.openDocument(document);
    (void)designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});

    UIElement* previewRoot   = designer.getPreviewRoot();
    UIElement* previewFirst  = designer.findByChildPath({0});
    UIElement* previewSecond = designer.findByChildPath({1});
    ASSERT_NE(previewRoot, nullptr);
    ASSERT_NE(previewFirst, nullptr);
    ASSERT_NE(previewSecond, nullptr);

    designer.applyWidgetDrop(previewSecond, *previewFirst, UIDesignerPanel::EDropPos::Into);

    EXPECT_EQ(previewRoot->getChildren().size(), 1u);
    UIElement* nestedParent = designer.findByChildPath({0});
    UIElement* nestedChild  = designer.findByChildPath({0, 0});
    ASSERT_NE(nestedParent, nullptr);
    ASSERT_NE(nestedChild, nullptr);
    EXPECT_EQ(nestedParent->_name, "First");
    EXPECT_EQ(nestedChild->_name, "Second");
}

TEST(UIDesignerPanelTest, DocumentRegistryTracksDirtyCloseAndSingleton)
{
    EditorDocumentRegistry documents;
    UIDesignerPanel        designer(nullptr);
    designer.bindDocuments(&documents);

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
    EXPECT_EQ(documents.size(), 0u);
}

TEST(UIDesignerPanelTest, OpenSceneEntrySharesDocumentSession)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    ASSERT_NE(root, nullptr);
    root->_name = "Root";
    auto document = UIDocument::fromWidget(*root);
    ASSERT_NE(document, nullptr);

    Scene scene("Level");
    SceneWidgetEntry entry;
    entry.entryId = "hud";
    entry.inlineDocument = document;

    EditorDocumentRegistry documents;
    UIDesignerPanel        first(nullptr);
    UIDesignerPanel        second(nullptr);
    first.bindDocuments(&documents);
    second.bindDocuments(&documents);
    first.openSceneEntry(scene, entry);
    second.openSceneEntry(scene, entry);

    ASSERT_NE(first.documentSession(), nullptr);
    EXPECT_EQ(first.documentSession(), second.documentSession());
    EXPECT_EQ(first.documentSession()->id(), makeEditorUIDocumentId("Level#hud"));
    EXPECT_TRUE(first.documentSession()->ownsPreview());
}

} // namespace ya
