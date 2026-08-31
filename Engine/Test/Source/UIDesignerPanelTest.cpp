// Regression guards for UIDesignerPanel's direct-manipulation path: when a
// preview child lives under a canvas host, drag edits must read/write the
// parent-owned slot edge consistently across multiple drags.

#include "GameEditor/Panels/UIDesignerPanel.h"

#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UITypeIds.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Layout/UILayout.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(UIDesignerPanelTest, ConsecutiveResizesUseTheCanvasSlotAsTheSourceOfTruth)
{
    auto& registry = UITypeRegistry::instance();
    auto root = registry.createInstance(kTypeIdPanel);
    auto child = registry.createInstance(kTypeIdPanel);
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

} // namespace ya
