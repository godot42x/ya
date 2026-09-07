// GAH-002: CPU-only baseline for specialized layout hosts that override
// layoutAssigned() and bypass UIElement::tryReuseAssignedLayout(). Does not
// introduce measure cache or unify the skip entry (GAH-201).

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

FCanvasSlotArgs fillCanvasArgs()
{
    FCanvasSlotArgs args;
    args.anchorMin = {0.0f, 0.0f};
    args.anchorMax = {1.0f, 1.0f};
    return args;
}

void addFillBoxChild(UIContainer& parent, const UIElementRef& child)
{
    parent.addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setSizeRule(EUIBoxSlotSizeRule::Fill);
        }
    });
}

void attachFill(WidgetTree& tree, const UIElementRef& widget)
{
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), widget, fillCanvasArgs()).valid());
}

void expectArrangeAfterParentReassign(WidgetTree& tree,
                                      UIElement&  host,
                                      UILayout&   layout,
                                      uint32_t    expected)
{
    tree.layout();
    layout.resetArrangeCount();
    UIElement* parent = host.getParent();
    ASSERT_NE(parent, nullptr) << host._name;
    parent->markArrangeDirty();
    tree.layout();
    EXPECT_EQ(layout.getArrangeCount(), expected) << host._name;
}

} // namespace

TEST(LayoutHostSkipBaselineTest, PanelSkipProofStillHoldsOnParentReassign)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       panel = std::make_shared<UIPanel>("Panel");
    attachFill(tree, panel);
    UILayout* layout = panel->getLayout();
    ASSERT_NE(layout, nullptr);

    expectArrangeAfterParentReassign(tree, *panel, *layout, 0u);
}

TEST(LayoutHostSkipBaselineTest, SpecializedHostsBypassAssignedLayoutSkip)
{
    WidgetTree tree({.width = 640, .height = 360});

    auto container = std::make_shared<UIContainer>("Container");
    auto button    = std::make_shared<UIButton>("Button");
    auto checkBox  = std::make_shared<UICheckBox>("CheckBox");
    auto overlay   = std::make_shared<UIOverlay>("Overlay");
    auto scroll    = std::make_shared<UIScrollViewport>("Scroll");
    auto split     = std::make_shared<UISplitPane>("Split");
    auto sizeBox   = std::make_shared<UISizeBox>("SizeBox");

    overlay->addDetachedChild(std::make_shared<UIPanel>("OverlayChild"));
    scroll->addDetachedChild(std::make_shared<UIPanel>("ScrollChild"), [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UIOverlaySlot*>(&slot)) {
            single->setPreferredSize({200.0f, 400.0f});
        }
    });
    split->addDetachedChild(std::make_shared<UIPanel>("PaneA"));
    split->addDetachedChild(std::make_shared<UIPanel>("PaneB"));
    sizeBox->addDetachedChild(std::make_shared<UIPanel>("SizeChild"));

    attachFill(tree, container);
    attachFill(tree, button);
    attachFill(tree, checkBox);
    attachFill(tree, overlay);
    attachFill(tree, scroll);
    attachFill(tree, split);
    attachFill(tree, sizeBox);

    expectArrangeAfterParentReassign(tree, *container, container->getBoxLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *button, button->getContentLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *checkBox, checkBox->getContentLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *overlay, overlay->getOverlayLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *scroll, scroll->getScrollLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *split, split->getSplitLayout(), 1u);
    expectArrangeAfterParentReassign(tree, *sizeBox, sizeBox->getContentLayout(), 1u);
}

TEST(LayoutHostSkipBaselineTest, PopupAndDockBypassAssignedLayoutSkip)
{
    WidgetTree tree({.width = 800, .height = 600});

    auto popup = std::make_shared<UIPopupOverlay>("Popup");
    auto panel = std::make_shared<UIPanel>("PopupContent");
    popup->_contentPos    = {16.0f, 12.0f};
    popup->_contentExtent = {120.0f, 48.0f};
    popup->addDetachedChild(panel);
    popup->open(tree);

    auto context = std::make_shared<FDockContext>();
    auto dock    = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    attachFill(tree, dock);
    ASSERT_NE(context->addPanel("scene", "Scene", std::make_shared<UIPanel>("SceneBody")),
              kInvalidDockPanelId);

    UILayout* popupLayout = popup->getLayout();
    UILayout* dockLayout  = dock->getLayout();
    ASSERT_NE(popupLayout, nullptr);
    ASSERT_NE(dockLayout, nullptr);

    expectArrangeAfterParentReassign(tree, *popup, *popupLayout, 1u);
    expectArrangeAfterParentReassign(tree, *dock, *dockLayout, 1u);
}

TEST(LayoutHostSkipBaselineTest, CleanSiblingContainerStillArrangesWhenOtherBranchDirties)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       row   = std::make_shared<UIContainer>("Row");
    auto       left  = std::make_shared<UIContainer>("Left");
    auto       right = std::make_shared<UIContainer>("Right");
    auto       dirty = std::make_shared<UIPanel>("Dirty");
    auto       clean = std::make_shared<UIPanel>("Clean");
    row->setDirection(EWidgetBoxLayout::Horizontal);

    attachFill(tree, row);
    addFillBoxChild(*row, left);
    addFillBoxChild(*row, right);
    addFillBoxChild(*left, dirty);
    addFillBoxChild(*right, clean);
    tree.layout();

    left->getBoxLayout().resetArrangeCount();
    right->getBoxLayout().resetArrangeCount();
    if (UILayout* dirtyLayout = dirty->getLayout()) {
        dirtyLayout->resetArrangeCount();
    }
    if (UILayout* cleanLayout = clean->getLayout()) {
        cleanLayout->resetArrangeCount();
    }

    EXPECT_FALSE(right->isMeasureDirty());
    EXPECT_FALSE(right->isArrangeDirty());
    dirty->markLayoutDirty(EUIInvalidationReason::LayoutProperty);
    EXPECT_TRUE(left->isMeasureDirty());
    EXPECT_FALSE(right->isMeasureDirty());
    tree.layout();

    EXPECT_EQ(left->getBoxLayout().getArrangeCount(), 1u);
    EXPECT_EQ(right->getBoxLayout().getArrangeCount(), 1u);
    ASSERT_NE(dirty->getLayout(), nullptr);
    ASSERT_NE(clean->getLayout(), nullptr);
    EXPECT_EQ(dirty->getLayout()->getArrangeCount(), 1u);
    EXPECT_EQ(clean->getLayout()->getArrangeCount(), 0u);
}

TEST(LayoutHostSkipBaselineTest, CleanSiblingPanelSkipsWhenOtherBranchDirties)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       first  = std::make_shared<UIPanel>("First");
    auto       second = std::make_shared<UIPanel>("Second");
    FCanvasSlotArgs firstArgs;
    firstArgs.fixedSize = {40.0f, 20.0f};
    FCanvasSlotArgs secondArgs;
    secondArgs.offset    = {80.0f, 0.0f};
    secondArgs.fixedSize = {40.0f, 20.0f};
    auto* layer = tree.getLayer(WidgetTree::ELayer::Content);
    ASSERT_TRUE(tree.attach(*layer, first, firstArgs).valid());
    ASSERT_TRUE(tree.attach(*layer, second, secondArgs).valid());
    tree.layout();

    ASSERT_NE(first->getLayout(), nullptr);
    ASSERT_NE(second->getLayout(), nullptr);
    first->getLayout()->resetArrangeCount();
    second->getLayout()->resetArrangeCount();

    auto* firstSlot = first->getSlot()->as<UICanvasSlot>();
    ASSERT_NE(firstSlot, nullptr);
    firstSlot->setOffset({16.0f, 12.0f});
    tree.layout();

    EXPECT_EQ(first->getLayout()->getArrangeCount(), 1u);
    EXPECT_EQ(second->getLayout()->getArrangeCount(), 0u);
}

} // namespace ya
