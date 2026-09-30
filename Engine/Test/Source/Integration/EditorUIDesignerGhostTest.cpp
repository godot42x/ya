// Designer-preview ghost contract: widgets the runtime would not render still
// paint in the designer's preview (dimmed), so a document that ships Hidden
// reads as a preview instead of a blank canvas. Runtime contexts keep the skip.

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

    static constexpr std::string_view kDocumentPath = "Test/UI/Ghost.yaui";

    FDesignerFixture() { layer.bindDocumentServices(&documents, &uiDocuments); }

    void publish(const UIElement& widget)
    {
        uiDocuments.put(kDocumentPath, UIDocument::fromWidget(widget));
    }
};

TEST(EditorUIDesignerGhostTest, HiddenRootStillPaintsInTheDesignerPreview)
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

    const UIFrameSnapshot snapshot = designer.buildPreviewSnapshot({1.0f, 1.0f}, {0.0f, 0.0f});
    ASSERT_FALSE(snapshot.items.empty());
    bool bFoundBorderRect = false;
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (item.kind == UIFrameDrawItem::EKind::Sprite && item.pos == glm::vec2(20.0f, 30.0f) &&
            item.size == glm::vec2(80.0f, 40.0f)) {
            bFoundBorderRect = true;
            // Ghosted, not full opacity: the dimmed read is the point.
            EXPECT_LT(item.color.a, 1.0f);
        }
    }
    EXPECT_TRUE(bFoundBorderRect);
}

TEST(EditorUIDesignerGhostTest, RuntimeBuildContextKeepsSkippingHiddenWidgets)
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
    tree.attachToLayer(WidgetTree::ELayer::Content, root, fillArgs);

    UIFrameBuildContext ctx; // ghostInvisibleOpacity defaults to 0: runtime skip
    const UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);
    EXPECT_TRUE(snapshot.items.empty());

    // The designer opt-in paints the same tree.
    ctx.ghostInvisibleOpacity = 0.35f;
    const UIFrameSnapshot ghosted = tree.buildSnapshot(ctx);
    EXPECT_FALSE(ghosted.items.empty());
}

// The tree's eye: presenter-owned display toggles prune whole subtrees from
// the canvas preview only. Runtime visibility is untouched, and the toggles
// reset when the preview is rebuilt or structurally edited (child paths are
// positional).
TEST(EditorUIDesignerGhostTest, DesignerDisplayTogglePrunesPreviewSubtreeOnly)
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
