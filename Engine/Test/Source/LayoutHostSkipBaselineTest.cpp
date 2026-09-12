// GAH-201 / GAH-202: assigned-layout skip is a single UIElement entry.
// Specialized hosts bind a member layout or override applyAssignedLayout();
// they must not bypass tryReuseAssignedLayout(). Does not introduce measure cache.

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
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

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

void addAutoBoxChild(UIContainer& parent, const UIElementRef& child)
{
    parent.addDetachedChild(child);
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

std::shared_ptr<Font> registerSkipTestFont()
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = 16.0f;
    font->lineHeight = 20.0f;
    font->ascent     = 16.0f;
    font->descent    = 4.0f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.size     = {8, 16};
        ch.bearing  = {0, 0};
        ch.advance  = {8.0f, 0.0f};
        ch.bInAtlas = true;
        font->characters[cp] = ch;
    }
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, font);
    return font;
}

} // namespace

TEST(LayoutHostSkipBaselineTest, PanelSkipProofStillHoldsOnParentReassign)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       panel = std::make_shared<UICanvasPanel>("Panel");
    attachFill(tree, panel);
    UILayout* layout = panel->getLayout();
    ASSERT_NE(layout, nullptr);

    expectArrangeAfterParentReassign(tree, *panel, *layout, 0u);
}

TEST(LayoutHostSkipBaselineTest, SpecializedHostsSkipOnParentReassign)
{
    WidgetTree tree({.width = 640, .height = 360});

    auto container = std::make_shared<UIContainer>("Container");
    auto button    = std::make_shared<UIButton>("Button");
    auto checkBox  = std::make_shared<UICheckBox>("CheckBox");
    auto overlay   = std::make_shared<UIOverlay>("Overlay");
    auto scroll    = std::make_shared<UIScrollViewport>("Scroll");
    auto split     = std::make_shared<UISplitPane>("Split");
    auto sizeBox   = std::make_shared<UISizeBox>("SizeBox");
    auto row       = std::make_shared<UISelectableRow>("Row");

    overlay->addDetachedChild(std::make_shared<UICanvasPanel>("OverlayChild"));
    scroll->addDetachedChild(std::make_shared<UICanvasPanel>("ScrollChild"), [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UIContentSlot*>(&slot)) {
            single->setPreferredSize({200.0f, 400.0f});
        }
    });
    split->addDetachedChild(std::make_shared<UICanvasPanel>("PaneA"));
    split->addDetachedChild(std::make_shared<UICanvasPanel>("PaneB"));
    sizeBox->addDetachedChild(std::make_shared<UICanvasPanel>("SizeChild"));

    attachFill(tree, container);
    attachFill(tree, button);
    attachFill(tree, checkBox);
    attachFill(tree, overlay);
    attachFill(tree, scroll);
    attachFill(tree, split);
    attachFill(tree, sizeBox);
    attachFill(tree, row);

    expectArrangeAfterParentReassign(tree, *container, container->getBoxLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *button, button->getContentLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *checkBox, checkBox->getContentLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *overlay, overlay->getOverlayLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *scroll, scroll->getScrollLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *split, split->getSplitLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *sizeBox, sizeBox->getContentLayout(), 0u);
    expectArrangeAfterParentReassign(tree, *row, row->getContentLayout(), 0u);
}

TEST(LayoutHostSkipBaselineTest, PopupAndDockSkipOnParentReassign)
{
    WidgetTree tree({.width = 800, .height = 600});

    auto popup = std::make_shared<UIPopupOverlay>("Popup");
    auto panel = std::make_shared<UICanvasPanel>("PopupContent");
    popup->_contentPos    = {16.0f, 12.0f};
    popup->_contentExtent = {120.0f, 48.0f};
    popup->addDetachedChild(panel);
    popup->open(tree);

    auto context = std::make_shared<FDockContext>();
    auto dock    = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    attachFill(tree, dock);
    ASSERT_NE(context->addPanel("scene", "Scene", std::make_shared<UICanvasPanel>("SceneBody")),
              kInvalidDockPanelId);

    UILayout* popupLayout = popup->getLayout();
    UILayout* dockLayout  = dock->getLayout();
    ASSERT_NE(popupLayout, nullptr);
    ASSERT_NE(dockLayout, nullptr);

    expectArrangeAfterParentReassign(tree, *popup, *popupLayout, 0u);
    expectArrangeAfterParentReassign(tree, *dock, *dockLayout, 0u);
}

TEST(LayoutHostSkipBaselineTest, CleanSiblingContainerSkipsWhenOtherBranchDirties)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto       row   = std::make_shared<UIContainer>("Row");
    auto       left  = std::make_shared<UIContainer>("Left");
    auto       right = std::make_shared<UIContainer>("Right");
    auto       dirty = std::make_shared<UICanvasPanel>("Dirty");
    auto       clean = std::make_shared<UICanvasPanel>("Clean");
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
    EXPECT_EQ(right->getBoxLayout().getArrangeCount(), 0u);
    ASSERT_NE(dirty->getLayout(), nullptr);
    ASSERT_NE(clean->getLayout(), nullptr);
    EXPECT_EQ(dirty->getLayout()->getArrangeCount(), 1u);
    EXPECT_EQ(clean->getLayout()->getArrangeCount(), 0u);
}

TEST(LayoutHostSkipBaselineTest, CleanSiblingPanelSkipsWhenOtherBranchDirties)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       first  = std::make_shared<UICanvasPanel>("First");
    auto       second = std::make_shared<UICanvasPanel>("Second");
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

TEST(LayoutHostSkipBaselineTest, DesiredSizeChangeRearrangesHost)
{
    registerSkipTestFont();
    WidgetTree tree({.width = 320, .height = 160});
    auto       column = std::make_shared<UIContainer>("Column");
    auto       label  = std::make_shared<UIText>("Label");
    label->setText("Hi");
    attachFill(tree, column);
    addAutoBoxChild(*column, label);
    tree.layout();

    column->getBoxLayout().resetArrangeCount();
    label->setText("Hello world");
    tree.layout();
    EXPECT_EQ(column->getBoxLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, SlotChangeRearrangesHost)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       column = std::make_shared<UIContainer>("Column");
    auto       child  = std::make_shared<UICanvasPanel>("Child");
    attachFill(tree, column);
    addAutoBoxChild(*column, child);
    tree.layout();

    UIBoxSlot* slot = column->getBoxSlot(*child);
    ASSERT_NE(slot, nullptr);
    column->getBoxLayout().resetArrangeCount();
    slot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    tree.layout();
    EXPECT_EQ(column->getBoxLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, VisibilityCollapseRearrangesHost)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       column = std::make_shared<UIContainer>("Column");
    auto       child  = std::make_shared<UICanvasPanel>("Child");
    attachFill(tree, column);
    addFillBoxChild(*column, child);
    tree.layout();

    column->getBoxLayout().resetArrangeCount();
    child->setVisibility(EWidgetVisibility::Collapsed);
    tree.layout();
    EXPECT_EQ(column->getBoxLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, StructureChangeRearrangesHost)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       column = std::make_shared<UIContainer>("Column");
    attachFill(tree, column);
    addFillBoxChild(*column, std::make_shared<UICanvasPanel>("First"));
    tree.layout();

    column->getBoxLayout().resetArrangeCount();
    addFillBoxChild(*column, std::make_shared<UICanvasPanel>("Second"));
    tree.layout();
    EXPECT_EQ(column->getBoxLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, ScrollOffsetChangeRearrangesViewport)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       scroll = std::make_shared<UIScrollViewport>("Scroll");
    scroll->addDetachedChild(std::make_shared<UICanvasPanel>("Content"), [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UIContentSlot*>(&slot)) {
            single->setPreferredSize({200.0f, 400.0f});
        }
    });
    attachFill(tree, scroll);
    tree.layout();

    scroll->getScrollLayout().resetArrangeCount();
    scroll->setScrollOffset(24.0f);
    tree.layout();
    EXPECT_EQ(scroll->getScrollLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, SplitRatioChangeRearrangesPane)
{
    WidgetTree tree({.width = 320, .height = 160});
    auto       split = std::make_shared<UISplitPane>("Split");
    split->addDetachedChild(std::make_shared<UICanvasPanel>("PaneA"));
    split->addDetachedChild(std::make_shared<UICanvasPanel>("PaneB"));
    attachFill(tree, split);
    tree.layout();

    split->getSplitLayout().resetArrangeCount();
    split->setSplitRatio(0.25f);
    tree.layout();
    EXPECT_EQ(split->getSplitLayout().getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, PopupContentSlotChangeRearrangesOverlay)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       popup = std::make_shared<UIPopupOverlay>("Popup");
    popup->_contentPos    = {16.0f, 12.0f};
    popup->_contentExtent = {120.0f, 48.0f};
    popup->addDetachedChild(std::make_shared<UICanvasPanel>("Content"));
    popup->open(tree);
    tree.layout();

    UILayout* layout = popup->getLayout();
    ASSERT_NE(layout, nullptr);
    layout->resetArrangeCount();
    UIElement* parent = popup->getParent();
    ASSERT_NE(parent, nullptr);
    popup->_contentPos = {40.0f, 50.0f};
    parent->markArrangeDirty();
    EXPECT_FALSE(popup->isArrangeDirty());
    tree.layout();
    EXPECT_EQ(layout->getArrangeCount(), 1u);
}

TEST(LayoutHostSkipBaselineTest, DockProjectionChangeRearrangesSpace)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       context = std::make_shared<FDockContext>();
    auto       dock    = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    attachFill(tree, dock);
    dock->addPanel("scene", std::make_shared<UICanvasPanel>("SceneBody"));
    tree.layout();

    UILayout* layout = dock->getLayout();
    ASSERT_NE(layout, nullptr);
    layout->resetArrangeCount();
    dock->addPanel("inspector", std::make_shared<UICanvasPanel>("InspectorBody"));
    tree.layout();
    EXPECT_EQ(layout->getArrangeCount(), 1u);
}

} // namespace ya
