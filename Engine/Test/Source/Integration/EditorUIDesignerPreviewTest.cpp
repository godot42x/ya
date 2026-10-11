// Designer-preview visibility contract: visibility is a runtime fact, so the
// designer's canvas edits the shown state -- widgets the runtime would skip
// still paint, at full opacity, and the tree-row eye is the editor's own hide.
// Runtime contexts keep the skip.

#include "GameEditor/EditorUIDesignerSession.h"
#include "GameEditor/EditorLayer.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Panel.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

struct FDesignerFixture
{
    EditorDocumentRegistry documents;
    UIDocumentStore        uiDocuments;
    EditorLayer            layer{nullptr};

    static constexpr std::string_view kDocumentPath = "Test/UI/Preview.yaui";

    FDesignerFixture() { layer.bindDocumentServices(&documents, &uiDocuments); }

    void publish(const UIElement& widget)
    {
        uiDocuments.put(kDocumentPath, UIDocument::fromWidget(widget));
    }
};

TEST(EditorUIDesignerPreviewTest, HiddenRootStillPaintsInTheDesignerPreview)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    root->_name       = "Dialogue";
    root->setVisibility(EWidgetVisibility::Hidden);
    auto border = registry.createInstance(kTypeIdBorder);
    border->_name = "Box";
    root->addDetachedChild(border, [](UIElement&, UISlot& edge) {
        FCanvasSlotArgs args;
        args.offset    = {20.0f, 30.0f};
        args.fixedSize = {80.0f, 40.0f};
        edge.as<UICanvasSlot>()->apply(args);
    });

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    ASSERT_TRUE(designer.hasDocument());

    // Reference alpha: the same border painted out of a Visible root -- the
    // shown state the canvas must reproduce, whatever the style's own alpha
    // is.
    auto visibleRoot = registry.createInstance(kTypeIdCanvasPanel);
    visibleRoot->_name = "Visible";
    auto visibleBorder = registry.createInstance(kTypeIdBorder);
    visibleBorder->_name = "Box";
    visibleRoot->addDetachedChild(visibleBorder, [](UIElement&, UISlot& edge) {
        FCanvasSlotArgs args;
        args.offset    = {20.0f, 30.0f};
        args.fixedSize = {80.0f, 40.0f};
        edge.as<UICanvasSlot>()->apply(args);
    });
    WidgetTree referenceTree(Extent2D{160u, 120u});
    FCanvasSlotArgs referenceSlot;
    referenceSlot.anchorMin = {0.0f, 0.0f};
    referenceSlot.anchorMax = {1.0f, 1.0f};
   (void)referenceTree.attachToLayer(WidgetTree::ELayer::Content, visibleRoot, referenceSlot);
    UIFrameBuildContext referenceCtx;
    referenceCtx.uiScale = {1.0f, 1.0f};
    const UIFrameSnapshot reference = referenceTree.buildSnapshot(referenceCtx);
    float visibleAlpha = -1.0f;
    for (const UIFrameDrawItem& item : reference.items) {
        if (item.kind == UIFrameDrawItem::EKind::Sprite && item.pos == glm::vec2(20.0f, 30.0f) &&
            item.size == glm::vec2(80.0f, 40.0f)) {
            visibleAlpha = item.color.a;
        }
    }
    ASSERT_GT(visibleAlpha, 0.0f);

    const UIFrameSnapshot snapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    ASSERT_FALSE(snapshot.items.empty());
    bool bFoundBorderRect = false;
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (item.kind == UIFrameDrawItem::EKind::Sprite && item.pos == glm::vec2(20.0f, 30.0f) &&
            item.size == glm::vec2(80.0f, 40.0f)) {
            bFoundBorderRect = true;
            // The shown state: the Hidden root paints exactly what a Visible
            // one paints, not a dimmed ghost.
            EXPECT_FLOAT_EQ(item.color.a, visibleAlpha);
        }
    }
    EXPECT_TRUE(bFoundBorderRect);
}

TEST(EditorUIDesignerPreviewTest, RuntimeBuildContextKeepsSkippingHiddenWidgets)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    root->_name = "Root";
    auto border = registry.createInstance(kTypeIdBorder);
    border->_name = "Box";
    root->addDetachedChild(border, [](UIElement&, UISlot& edge) {
        FCanvasSlotArgs args;
        args.offset    = {10.0f, 10.0f};
        args.fixedSize = {50.0f, 25.0f};
        edge.as<UICanvasSlot>()->apply(args);
    });
    border->setVisibility(EWidgetVisibility::Hidden);

    WidgetTree tree(Extent2D{200u, 100u});
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
   (void)tree.attachToLayer(WidgetTree::ELayer::Content, root, fillArgs);

    UIFrameBuildContext ctx; // ghostInvisibleOpacity defaults to 0: runtime skip
    const UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);
    EXPECT_TRUE(snapshot.items.empty());

    // The designer opt-in paints the same tree, at the shown state.
    ctx.ghostInvisibleOpacity = 1.0f;
    const UIFrameSnapshot preview = tree.buildSnapshot(ctx);
    EXPECT_FALSE(preview.items.empty());
}

// The tree's eye: presenter-owned display toggles prune whole subtrees from
// the canvas preview only. Runtime visibility is untouched, and the toggles
// reset when the preview is rebuilt or structurally edited (child paths are
// positional).
TEST(EditorUIDesignerPreviewTest, DesignerDisplayTogglePrunesPreviewSubtreeOnly)
{
    auto& registry = UITypeRegistry::instance();
    auto  root     = registry.createInstance(kTypeIdCanvasPanel);
    root->_name = "Root";
    auto border = registry.createInstance(kTypeIdBorder);
    border->_name = "Box";
    root->addDetachedChild(border, [](UIElement&, UISlot& edge) {
        FCanvasSlotArgs args;
        args.offset    = {10.0f, 20.0f};
        args.fixedSize = {60.0f, 30.0f};
        edge.as<UICanvasSlot>()->apply(args);
    });
    auto sibling = registry.createInstance(kTypeIdBorder);
    sibling->_name = "Other";
    root->addDetachedChild(sibling, [](UIElement&, UISlot& edge) {
        FCanvasSlotArgs args;
        args.offset    = {100.0f, 20.0f};
        args.fixedSize = {60.0f, 30.0f};
        edge.as<UICanvasSlot>()->apply(args);
    });

    FDesignerFixture       fixture;
    EditorUIDesignerSession designer(&fixture.layer);
    fixture.publish(*root);
    designer.openDocument(FDesignerFixture::kDocumentPath);
    ASSERT_TRUE(designer.hasDocument());
    ASSERT_FALSE(designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f}).items.empty());

    // Eye off on the first child: its subtree drops out of the snapshot, the
    // sibling stays. Runtime visibility fields are untouched.
    designer.toggleDesignerHidden({0});
    ASSERT_TRUE(designer.isDesignerHidden({0}));
    const UIFrameSnapshot filtered = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    for (const UIFrameDrawItem& item : filtered.items) {
        EXPECT_FALSE(item.pos.x < 80.0f) << "hidden subtree painted at x="
                                         << item.pos.x;
    }
    EXPECT_FALSE(filtered.items.empty());

    // Eye back on: the subtree returns.
    designer.toggleDesignerHidden({0});
    EXPECT_FALSE(designer.isDesignerHidden({0}));
    EXPECT_FALSE(designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f}).items.empty());

    // A structural edit resets the toggles: child paths are positional.
    designer.toggleDesignerHidden({0});
    (void)designer.addPaletteWidget(kTypeIdBorder);
    EXPECT_FALSE(designer.isDesignerHidden({0}));
}

} // namespace
} // namespace ya
