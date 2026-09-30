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

} // namespace
} // namespace ya
