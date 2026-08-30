// Layout regression suite (gui-app-bootstrap foundation). Guards the
// SizeToContent / AutoSize contract end to end: text measurement -> button /
// composite control sizing -> container aggregation -> scroll/split
// propagation -> workbench shell fixture.
//
// The target links ONLY the GUI closure; fonts are injected through
// FontManager::registerFont (synthetic glyph data, no GPU).

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Overlay.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/DockFloatingHost.h"
#include "GUI/Widgets/Controls/DockFloatingWindow.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace ya
{

namespace
{

// --- Synthetic font injection -------------------------------------------------
// Every ASCII glyph advances `advancePerChar` px; lineHeight fixed per size.
std::shared_ptr<Font> makeSyntheticFont(float fontSize, float advancePerChar)
{
    auto font      = std::make_shared<Font>();
    font->fontSize = fontSize;
    font->lineHeight = fontSize * 1.25f;
    font->ascent     = fontSize;
    font->descent    = fontSize * 0.25f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.uvRect     = {};
        ch.size       = {static_cast<int>(advancePerChar), static_cast<int>(fontSize)};
        ch.bearing    = {0, 0};
        ch.advance    = {advancePerChar, 0.0f};
        ch.bInAtlas   = true;
        font->characters[cp] = ch;
    }
    return font;
}

/// Register a synthetic font under RuntimeDefault:fontSize; returns it.
std::shared_ptr<Font> registerSyntheticFont(uint32_t fontSize = 16, float advancePerChar = 8.0f)
{
    auto font = makeSyntheticFont(static_cast<float>(fontSize), advancePerChar);
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, fontSize, font);
    return font;
}

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

// --- Test tree builder helpers -------------------------------------------------

std::shared_ptr<UIText> makeAutoText(const std::string& text, uint32_t fontSize = 16)
{
    auto label = std::make_shared<UIText>(text + "_Label");
    label->setText(text);
    label->_fontSize  = fontSize;
    label->_bAutoSize = true;
    return label;
}

struct FSizeProbe final : UIPanel
{
    using UIPanel::UIPanel;
    void corruptSize(glm::vec2 value) { _size = value; }
};

std::shared_ptr<UIButton> makeAutoButton(const std::string& name, const std::string& labelText)
{
    auto button = std::make_shared<UIButton>(name);
    button->_bAutoSize = true;
    button->setContentPadding({10.0f, 4.0f});
    auto label = makeAutoText(labelText);
    label->_hAlign = EWidgetAlignH::Center;
    label->_vAlign = EWidgetAlignV::Center;
    button->addDetachedChild(label);
    return button;
}

} // namespace

// === SizeToContent contract on UIElement ===

TEST(WidgetLayoutTest, TextAutoSizeMeasuresGlyphWidth)
{
    registerSyntheticFont(16, 8.0f);
    auto label = makeAutoText("Hello", 16);
    const glm::vec2 desired = label->computeDesiredSize();
    // "Hello" = 5 glyphs x 8px advance; lineHeight = 16 * 1.25.
    EXPECT_FLOAT_EQ(desired.x, 5.0f * 8.0f);
    EXPECT_FLOAT_EQ(desired.y, 16.0f * 1.25f);
}

TEST(WidgetLayoutTest, TextWithoutAutoSizeStillMeasuresGlyphs)
{
    registerSyntheticFont(16, 8.0f);
    auto label = std::make_shared<UIText>("Fixed");
    label->setText("Hello");
    label->_fontSize = 16;
    label->setSize({120.0f, 24.0f});
    EXPECT_EQ(label->getSize(), glm::vec2(120.0f, 24.0f));
    EXPECT_FLOAT_EQ(label->computeDesiredSize().x, 5.0f * 8.0f);
    EXPECT_FLOAT_EQ(label->computeDesiredSize().y, 16.0f * 1.25f);
    EXPECT_EQ(label->computeIntrinsicSize(), label->computeDesiredSize());
}

TEST(WidgetLayoutTest, TextFieldIntrinsicSizeTracksContent)
{
    registerSyntheticFont(16, 8.0f);
    auto field = std::make_shared<UITextField>("Field");
    field->setText("Hello");
    field->_fontSize = 16;
    EXPECT_FLOAT_EQ(field->computeIntrinsicSize().x, 40.0f);
    EXPECT_FLOAT_EQ(field->computeIntrinsicSize().y, 20.0f);
}

TEST(WidgetLayoutTest, SpecializedViewsExposeExplicitIntrinsicSize)
{
    auto treeView = std::make_shared<UITreeView>("Tree");
    treeView->setSize({240.0f, 180.0f});
    EXPECT_EQ(treeView->computeIntrinsicSize(), glm::vec2(240.0f, 180.0f));

    auto table = std::make_shared<UITableGrid>("Table");
    table->setSize({320.0f, 120.0f});
    EXPECT_EQ(table->computeIntrinsicSize(), glm::vec2(320.0f, 120.0f));
}

TEST(WidgetLayoutTest, AttachedSpecializedViewsReportContentToParentSlot)
{
    WidgetTree tree({.width = 800, .height = 600});

    auto treeView = std::make_shared<UITreeView>("Tree");
    treeView->setSize({240.0f, 180.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, treeView);
    EXPECT_FLOAT_EQ(treeView->computeDesiredSize().y, 0.0f);

    auto table = std::make_shared<UITableGrid>("Table");
    table->setSize({320.0f, 120.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, table);
    EXPECT_FLOAT_EQ(table->computeDesiredSize().x, 320.0f);
    EXPECT_FLOAT_EQ(table->computeDesiredSize().y, table->_rowHeight);
}

TEST(WidgetLayoutTest, AutoSizeActivityComesFromTheParentSlot)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto label = std::make_shared<UIText>("Label");
    EXPECT_FALSE(label->isAutoSizeActive());

    auto column = std::make_shared<UIContainer>("Column");
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    column->addDetachedChild(label);
    EXPECT_TRUE(label->isAutoSizeActive());

    if (auto* slot = dynamic_cast<UIBoxSlot*>(column->getSlotForChild(*label))) {
        slot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    }
    EXPECT_FALSE(label->isAutoSizeActive());
}

TEST(WidgetLayoutTest, WrappedTextWithoutExplicitWidthKeepsIntrinsicWidth)
{
    registerSyntheticFont(16, 8.0f);
    auto label = std::make_shared<UIText>("Wrapped");
    label->setText("Hello world");
    label->_fontSize = 16;
    label->_bWrap = true;
    const glm::vec2 intrinsic = label->computeIntrinsicSize();
    EXPECT_FLOAT_EQ(intrinsic.x, 11.0f * 8.0f);
    EXPECT_FLOAT_EQ(intrinsic.y, 20.0f);
}

TEST(WidgetLayoutTest, AnchorLayoutResolvesStretchOverAutoOverSize)
{
    WidgetTree tree({.width = 400, .height = 300});

    // AutoSize in anchor layout: size resolves from desired (text measure).
    auto text = makeAutoText("Hello", 16);
    text->setSize({999.0f, 999.0f}); // must be ignored
    tree.attachToLayer(WidgetTree::ELayer::Content, text);
    tree.layout();
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.x, 40.0f);
    EXPECT_FLOAT_EQ(text->_layoutRect.extent.y, 20.0f);

    // Stretch wins over AutoSize on the stretch axis.
    auto stretch = std::make_shared<UIPanel>("Stretch");
    stretch->_bAutoSize = true;
    stretch->_anchorMin = {0.0f, 0.0f};
    stretch->_anchorMax = {1.0f, 0.0f};
    stretch->setPosition({0.0f, 60.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, stretch);
    tree.layout();
    EXPECT_FLOAT_EQ(stretch->_layoutRect.extent.x, 400.0f); // stretched
}

// === Button content-slot sizing ===

TEST(WidgetLayoutTest, ButtonSizesToTextContentWithPadding)
{
    registerSyntheticFont(16, 8.0f);
    auto button = makeAutoButton("Btn", "Hello");
    const glm::vec2 desired = button->computeDesiredSize();
    // text 5x8=40 + padding 2x10; height line 20 + 2x4.
    EXPECT_FLOAT_EQ(desired.x, 40.0f + 20.0f);
    EXPECT_FLOAT_EQ(desired.y, 20.0f + 8.0f);
}

TEST(WidgetLayoutTest, EmptyAutoButtonSizesToContentPadding)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto button = std::make_shared<UIButton>("Empty");
    button->_bAutoSize = true;
    button->setContentPadding({12.0f, 6.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
    tree.layout();

    EXPECT_EQ(button->computeDesiredSize(), glm::vec2(24.0f, 12.0f));
    EXPECT_EQ(button->_layoutRect.extent, glm::vec2(24.0f, 12.0f));
}

TEST(WidgetLayoutTest, ButtonAutoSizeInContainerPacksAndFills)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 500, .height = 200});
    auto toolbar = std::make_shared<UIContainer>("Toolbar");
    toolbar->_anchorMin = {0.0f, 0.0f};
    toolbar->_anchorMax = {1.0f, 0.0f};
    toolbar->setSize({0.0f, 40.0f});
    toolbar->setDirection(EWidgetBoxLayout::Horizontal);
    toolbar->setSpacing(8.0f);
    toolbar->setPadding({6.0f, 6.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, toolbar);

    auto addBtn = makeAutoButton("Add", "Add");
    auto resetBtn = makeAutoButton("Reset", "Reset Layout");
    tree.attach(*toolbar, addBtn);
    tree.attach(*toolbar, resetBtn);
    tree.layout();

    // Width follows text + padding: "Add" = 3x8=24 + 20 = 44.
    EXPECT_FLOAT_EQ(addBtn->_layoutRect.extent.x, 44.0f);
    // "Reset Layout" = 12x8=96 + 20 = 116.
    EXPECT_FLOAT_EQ(resetBtn->_layoutRect.extent.x, 116.0f);
    // Cross axis stretches to the toolbar content rect height:
    // 40 - 2*6(padding) = 28.
    EXPECT_FLOAT_EQ(addBtn->_layoutRect.extent.y, 28.0f);

    // Label fills the button content rect and is centered by paint alignment.
    auto* label = dynamic_cast<UIText*>(addBtn->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->_layoutRect.pos.x, addBtn->_layoutRect.pos.x + 10.0f);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 24.0f); // text width
}

TEST(WidgetLayoutTest, ButtonExplicitSizeInContainerKeepsItsWidth)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 200});
    auto row = std::make_shared<UIContainer>("Row");
    row->setDirection(EWidgetBoxLayout::Horizontal);
    row->_anchorMin = {0.0f, 0.0f};
    row->_anchorMax = {1.0f, 0.0f};
    row->setSize({0.0f, 40.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, row);

    // Explicit-size button must NOT be pushed by its text content when
    // AutoSize is off (container packing uses computeDesiredSize).
    auto grow = makeAutoButton("Grow", "Grow +20");
    grow->_bAutoSize = false;
    grow->setSize({90.0f, 24.0f});
    auto shrink = makeAutoButton("Shrink", "Shrink -20");
    shrink->_bAutoSize = false;
    shrink->setSize({100.0f, 24.0f});
    row->addDetachedChild(grow, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({90.0f, 24.0f});
        }
    });
    row->addDetachedChild(shrink, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({100.0f, 24.0f});
        }
    });
    tree.layout();

    EXPECT_FLOAT_EQ(grow->_layoutRect.extent.x, 90.0f);
    EXPECT_FLOAT_EQ(shrink->_layoutRect.extent.x, 100.0f);
    // Sibling starts after explicit width + spacing.
    EXPECT_FLOAT_EQ(shrink->_layoutRect.pos.x - grow->_layoutRect.pos.x - 90.0f, 4.0f);
}

TEST(WidgetLayoutTest, ButtonExplicitSizeIgnoresContentWidth)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 200});
    auto button = makeAutoButton("Wide", "Hello");
    button->_bAutoSize = false;
    button->setSize({200.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
    tree.layout();
    EXPECT_FLOAT_EQ(button->_layoutRect.extent.x, 200.0f);
    EXPECT_FLOAT_EQ(button->_layoutRect.extent.y, 32.0f);
    // Content child still arranged inside the padded rect.
    auto* label = dynamic_cast<UIText*>(button->getChildren()[0].get());
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 200.0f - 20.0f);
}

// === Pure SizeToContent chain: children push the parent ===

TEST(WidgetLayoutTest, ContainerAutoSizesToChildrenSum)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 300});
    auto vbox = std::make_shared<UIContainer>("VBox");
    vbox->_bAutoSize  = true;
    vbox->setDirection(EWidgetBoxLayout::Vertical);
    vbox->setSpacing(4.0f);
    vbox->setPadding({8.0f, 8.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, vbox);

    auto btnA = makeAutoButton("A", "One");
    auto btnB = makeAutoButton("B", "Longer Label");
    tree.attach(*vbox, btnA);
    tree.attach(*vbox, btnB);
    tree.layout();

    // btnA desired height = 20 + 8 = 28; btnB same; + spacing 4 + padding 16.
    EXPECT_FLOAT_EQ(vbox->_layoutRect.extent.y, 28.0f * 2.0f + 4.0f + 16.0f);
    // Width = max cross (both buttons 28 tall? no - cross is x for vertical:
    // widest child desired x) + padding.
    // btnB desired x = "Longer Label"(12x8=96)+20 = 116.
    EXPECT_FLOAT_EQ(vbox->_layoutRect.extent.x, 116.0f + 16.0f);

    // Children laid out sequentially with spacing.
    EXPECT_FLOAT_EQ(btnB->_layoutRect.pos.y - btnA->_layoutRect.pos.y - btnA->_layoutRect.extent.y, 4.0f);
}

// === Container packing with auto children (regression) ===

TEST(WidgetLayoutTest, NestedContainersPropagateDesiredSizes)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 600, .height = 400});
    auto hbox = std::make_shared<UIContainer>("HBox");
    hbox->_bAutoSize = true;
    hbox->setDirection(EWidgetBoxLayout::Horizontal);
    hbox->setSpacing(6.0f);
    hbox->setPadding({4.0f, 4.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, hbox);

    auto innerV = std::make_shared<UIContainer>("InnerV");
    innerV->_bAutoSize = true;
    innerV->setDirection(EWidgetBoxLayout::Vertical);
    innerV->setSpacing(2.0f);
    innerV->setPadding({0.0f, 0.0f});
    auto t1 = makeAutoText("AB", 16); // 2x8 = 16
    auto t2 = makeAutoText("CDE", 16); // 3x8 = 24
    innerV->addDetachedChild(t1);
    innerV->addDetachedChild(t2);

    auto spacer = std::make_shared<UIPanel>("Spacer");
    spacer->setSize({50.0f, 30.0f});

    tree.attach(*hbox, innerV);
    hbox->addDetachedChild(spacer, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({50.0f, 30.0f});
        }
    });
    tree.layout();

    // innerV desired = cross max(16,24) x, main 16+2+24=42 y.
    // hbox desired = innerV.x(24) + 50 + spacing 6 + padding 8 = 88 wide,
    // cross max(innerV 42, spacer 30) + 8 = 50 tall.
    EXPECT_FLOAT_EQ(hbox->_layoutRect.extent.x, 24.0f + 50.0f + 6.0f + 8.0f);
    EXPECT_FLOAT_EQ(hbox->_layoutRect.extent.y, 42.0f + 8.0f);
}

// === Scroll / split propagation ===

TEST(WidgetLayoutTest, ScrollViewportContentMainUsesAutoSizeChild)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 300, .height = 300});
    auto scroll = std::make_shared<UIScrollViewport>("Scroll");
    scroll->_anchorMin = {0.0f, 0.0f};
    scroll->_anchorMax = {1.0f, 1.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, scroll);

    auto content = std::make_shared<UIContainer>("Content");
    content->setDirection(EWidgetBoxLayout::Vertical);
    content->setSpacing(2.0f);
    for (int i = 0; i < 20; ++i) {
        content->addDetachedChild(makeAutoText("line", 16));
    }
    tree.attach(*scroll, content);
    tree.layout();

    // Content main (y) = 20 lines x lineHeight 20 + 19 x spacing 2 = 438,
    // taller than the 300 viewport: scrollable, content rect carries the
    // aggregated main size; cross axis stretches to the viewport width.
    ASSERT_EQ(content->_layoutRect.extent.y, 20.0f * 20.0f + 19.0f * 2.0f);
    EXPECT_FLOAT_EQ(content->_layoutRect.extent.x, 300.0f);
    EXPECT_GT(scroll->getMaxScrollOffset(), 0.0f);
}

TEST(WidgetLayoutTest, SplitPaneDesiredSizeAggregatesAutoChildren)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 300});
    auto split = std::make_shared<UISplitPane>("Split");
    split->_bAutoSize = true;
    split->setSplitRatio(0.5f);
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    auto left = makeAutoButton("L", "Left");
    auto right = makeAutoButton("R", "Right Side");
    tree.attach(*split, left);
    tree.attach(*split, right);
    tree.layout();

    // Split desired sums the two children along the split axis (horizontal):
    // "Left" = 4x8+20 = 52, "Right Side" = 10x8+20 = 100.
    EXPECT_GT(split->_layoutRect.extent.x, 150.0f);
    EXPECT_GT(split->_layoutRect.extent.y, 20.0f);
}

TEST(WidgetLayoutTest, BoxSlotsAreParentOwnedAndRecreatedOnReparent)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto first = std::make_shared<UIContainer>("First");
    auto second = std::make_shared<UIContainer>("Second");
    first->setSize({200.0f, 100.0f});
    second->setPosition({200.0f, 0.0f});
    second->setSize({200.0f, 100.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, first);
    tree.attachToLayer(WidgetTree::ELayer::Content, second);

    auto child = std::make_shared<UIPanel>("Child");
    child->setSize({20.0f, 20.0f});
    tree.attach(*first, child);

    auto* firstSlot = first->getBoxSlot(*child);
    ASSERT_NE(firstSlot, nullptr);
    EXPECT_EQ(child->getSlot(), firstSlot);
    tree.layout();
    EXPECT_TRUE(tree.isLayoutValid());
    firstSlot->setMargin({3.0f, 2.0f});
    EXPECT_FALSE(tree.isLayoutValid());

    tree.reparent(*second, child);
    auto* secondSlot = second->getBoxSlot(*child);
    ASSERT_NE(secondSlot, nullptr);
    EXPECT_EQ(child->getSlot(), secondSlot);
    EXPECT_EQ(&secondSlot->getParent(), second.get());
    EXPECT_EQ(&secondSlot->getChild(), child.get());
    EXPECT_EQ(first->getBoxSlot(*child), nullptr);

    tree.detach(*child);
    EXPECT_EQ(child->getSlot(), nullptr);
    EXPECT_EQ(second->getBoxSlot(*child), nullptr);
}

TEST(WidgetLayoutTest, BoxSlotFillMarginAndCrossAlignmentArrangeWithoutContainerFields)
{
    WidgetTree tree({.width = 300, .height = 120});
    auto box = std::make_shared<UIContainer>("Box");
    box->setSize({300.0f, 120.0f});
    box->setDirection(EWidgetBoxLayout::Horizontal);
    box->setPadding({10.0f, 10.0f});
    box->setSpacing(5.0f);
    tree.attachToLayer(WidgetTree::ELayer::Content, box);

    auto fixed = std::make_shared<UIPanel>("Fixed");
    fixed->setSize({50.0f, 20.0f});
    auto fill = std::make_shared<UIPanel>("Fill");
    fill->setSize({10.0f, 20.0f});
    box->addDetachedChild(fixed, [](UIElement&, UISlot& slot) {
        if (auto* boxSlot = dynamic_cast<UIBoxSlot*>(&slot)) {
            boxSlot->setPreferredSize({50.0f, 20.0f});
        }
    });
    box->addDetachedChild(fill, [](UIElement&, UISlot& slot) {
        if (auto* boxSlot = dynamic_cast<UIBoxSlot*>(&slot)) {
            boxSlot->setPreferredSize({10.0f, 20.0f});
        }
    });
    auto* fillSlot = box->getBoxSlot(*fill);
    ASSERT_NE(fillSlot, nullptr);
    fillSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    fillSlot->setMargin({4.0f, 0.0f});
    fillSlot->setCrossAlignment(EUIBoxSlotCrossAlignment::Center);

    tree.layout();
    EXPECT_FLOAT_EQ(fixed->_layoutRect.pos.x, 10.0f);
    EXPECT_FLOAT_EQ(fixed->_layoutRect.extent.x, 50.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.pos.x, 69.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.x, 217.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.y, 20.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.pos.y, 50.0f);

    const auto dump = dumpWidgetTree(tree);
    const auto* boxNode = findWidgetNode(dump, "Box");
    const auto* fillNode = findWidgetNode(dump, "Fill");
    ASSERT_NE(boxNode, nullptr);
    ASSERT_NE(fillNode, nullptr);
    EXPECT_EQ((*boxNode)["layout"]["type"], "box");
    EXPECT_EQ((*boxNode)["layout"]["direction"], "horizontal");
    EXPECT_EQ((*fillNode)["slot"]["sizeRule"], "fill");
}

TEST(WidgetLayoutTest, BoxSlotsKeepEdgeStateLocalAcrossNestedReparent)
{
    WidgetTree tree({.width = 320, .height = 180});
    auto outer = std::make_shared<UIContainer>("Outer");
    outer->setSize({320.0f, 180.0f});
    outer->setDirection(EWidgetBoxLayout::Vertical);
    outer->setPadding({10.0f, 10.0f});
    outer->setSpacing(4.0f);
    tree.attachToLayer(WidgetTree::ELayer::Content, outer);

    auto inner = std::make_shared<UIContainer>("Inner");
    inner->setDirection(EWidgetBoxLayout::Horizontal);
    auto sibling = std::make_shared<UIPanel>("Sibling");
    sibling->setSize({40.0f, 20.0f});
    auto child = std::make_shared<UIPanel>("Child");
    child->setSize({20.0f, 20.0f});

    tree.attach(*outer, inner);
    tree.attach(*outer, sibling);
    tree.attach(*inner, child);

    auto* innerSlot = outer->getBoxSlot(*inner);
    auto* childSlot = inner->getBoxSlot(*child);
    ASSERT_NE(innerSlot, nullptr);
    ASSERT_NE(childSlot, nullptr);
    innerSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    innerSlot->setWeight(2.0f);
    childSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    childSlot->setMargin({3.0f, 2.0f});

    tree.layout();
    EXPECT_EQ(child->getSlot(), childSlot);
    EXPECT_EQ(&childSlot->getParent(), inner.get());
    EXPECT_EQ(childSlot->getSizeRule(), EUIBoxSlotSizeRule::Fill);
    EXPECT_EQ(childSlot->getMargin(), FMargin::hv(3.0f, 2.0f));

    tree.reparent(*outer, child);
    auto* reparentedSlot = outer->getBoxSlot(*child);
    ASSERT_NE(reparentedSlot, nullptr);
    EXPECT_EQ(child->getSlot(), reparentedSlot);
    EXPECT_EQ(&reparentedSlot->getParent(), outer.get());
    EXPECT_EQ(inner->getBoxSlot(*child), nullptr);
    EXPECT_EQ(reparentedSlot->getSizeRule(), EUIBoxSlotSizeRule::Auto);
    EXPECT_EQ(reparentedSlot->getMargin(), FMargin{});

    reparentedSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    reparentedSlot->setWeight(1.0f);
    tree.layout();
    const auto dump = dumpWidgetTree(tree);
    const auto* childNode = findWidgetNode(dump, "Child");
    ASSERT_NE(childNode, nullptr);
    EXPECT_EQ((*childNode)["slot"]["parent"], "Outer");
    EXPECT_EQ((*childNode)["slot"]["sizeRule"], "fill");
}

TEST(WidgetLayoutTest, SameParentReorderPreservesExistingBoxSlotState)
{
    WidgetTree tree({.width = 320, .height = 180});
    auto box = std::make_shared<UIContainer>("Box");
    box->setSize({320.0f, 180.0f});
    box->setDirection(EWidgetBoxLayout::Horizontal);
    tree.attachToLayer(WidgetTree::ELayer::Content, box);

    auto first  = std::make_shared<UIPanel>("First");
    auto moved  = std::make_shared<UIPanel>("Moved");
    auto third  = std::make_shared<UIPanel>("Third");
    first->setSize({20.0f, 20.0f});
    moved->setSize({20.0f, 20.0f});
    third->setSize({20.0f, 20.0f});
    tree.attach(*box, first);
    tree.attach(*box, moved);
    tree.attach(*box, third);

    auto* movedSlot = box->getBoxSlot(*moved);
    ASSERT_NE(movedSlot, nullptr);
    movedSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    movedSlot->setWeight(2.0f);
    movedSlot->setMargin({3.0f, 2.0f});
    movedSlot->setCrossAlignment(EUIBoxSlotCrossAlignment::Center);

    tree.reparentAfter(*third, moved);

    EXPECT_EQ(box->getChildren()[0].get(), first.get());
    EXPECT_EQ(box->getChildren()[1].get(), third.get());
    EXPECT_EQ(box->getChildren()[2].get(), moved.get());
    EXPECT_EQ(box->getBoxSlot(*moved), movedSlot);
    EXPECT_EQ(moved->getSlot(), movedSlot);
    EXPECT_EQ(movedSlot->getSizeRule(), EUIBoxSlotSizeRule::Fill);
    EXPECT_EQ(movedSlot->getWeight(), 2.0f);
    EXPECT_EQ(movedSlot->getMargin(), FMargin::hv(3.0f, 2.0f));
    EXPECT_EQ(movedSlot->getCrossAlignment(), EUIBoxSlotCrossAlignment::Center);
}

TEST(WidgetLayoutTest, BoxSlotsControlHiddenParticipationAndFillBounds)
{
    WidgetTree tree({.width = 200, .height = 120});
    auto box = std::make_shared<UIContainer>("Box");
    box->setSize({200.0f, 120.0f});
    box->setDirection(EWidgetBoxLayout::Vertical);
    box->setMainAxisAlignment(EWidgetMainAxisAlignment::End);
    tree.attachToLayer(WidgetTree::ELayer::Content, box);

    auto hidden = std::make_shared<UIPanel>("Hidden");
    hidden->setSize({100.0f, 20.0f});
    hidden->setVisibility(EWidgetVisibility::Hidden);
    auto fill = std::make_shared<UIPanel>("Fill");
    fill->setSize({100.0f, 10.0f});
    tree.attach(*box, hidden);
    tree.attach(*box, fill);

    auto* hiddenSlot = box->getBoxSlot(*hidden);
    auto* fillSlot = box->getBoxSlot(*fill);
    ASSERT_NE(hiddenSlot, nullptr);
    ASSERT_NE(fillSlot, nullptr);
    hiddenSlot->setReserveSpaceWhenHidden(false);
    fillSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    fillSlot->setMinSize({0.0f, 30.0f});
    fillSlot->setMaxSize({std::numeric_limits<float>::max(), 40.0f});

    tree.layout();
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.y, 40.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.pos.y, 80.0f);
}

TEST(WidgetLayoutTest, BoxLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry)
{
    registerSyntheticFont(16, 8.0f);

    auto zone = ui::text("Zone").setText("Hi").release();
    UIElement* const zoneRaw = zone.get();
    ASSERT_NE(zoneRaw, nullptr);
    const glm::vec2 originalSize = zoneRaw->getSize();

    auto column = ui::column("Column")
                      .setSize({300.0f, 200.0f})
                      [ui::layout().size({0.0f, 120.0f}) >> zone]
                      .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    tree.layout();

    const UIElement* child = column->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);

    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(0.0f, 120.0f));
    EXPECT_EQ(zoneRaw->getSize(), originalSize)
        << "box layout intent must stay on the slot, not overwrite the child geometry";
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 120.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 300.0f);
}

// === GI-103: UIText resolved measure/paint + AutoSize Layout edge ===

TEST(WidgetLayoutTest, AutoSizeTextBindingTriggersLayoutOnTextChange)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 200});

    auto ref   = std::make_shared<Reactive<std::string>>("Hi");   // 2x8 = 16
    auto label = std::make_shared<UIText>("AutoBound");
    label->_bAutoSize = true;
    label->bindText(ref);
    tree.attachToLayer(WidgetTree::ELayer::Content, label);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start (lay out + collect edge)
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 16.0f);

    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    ref->set("Hello World");                       // 11x8 = 88 -> desired width changes
    tree.buildSnapshot(UIFrameBuildContext{});     // re-layout + re-paint

    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 88.0f);
}

TEST(WidgetLayoutTest, FixedSizeTextBindingTriggersPaintOnly)
{
    registerSyntheticFont(16, 8.0f);
    WidgetTree tree({.width = 400, .height = 200});

    auto ref   = std::make_shared<Reactive<std::string>>("Hi");
    auto label = std::make_shared<UIText>("FixedBound");
    label->_bAutoSize = false;
    label->setSize({100.0f, 24.0f});
    label->bindText(ref);
    tree.attachToLayer(WidgetTree::ELayer::Content, label);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    ref->set("Hello World"); // content-only change: fixed size, paint-only
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore);
    // Explicit size is untouched (no AutoSize measure).
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 100.0f);
}

TEST(WidgetLayoutTest, AutoSizeTextComputeDesiredSizeUsesResolvedText)
{
    registerSyntheticFont(16, 8.0f);
    auto ref   = std::make_shared<Reactive<std::string>>("Hi");
    auto label = std::make_shared<UIText>("AutoMeasure");
    label->_bAutoSize = true;
    label->bindText(ref);

    // No paint walk here: measure must still read the resolved (bound) text,
    // not the stale _text field.
    ref->set("Hello World");
    EXPECT_FLOAT_EQ(label->computeDesiredSize().x, 88.0f);
}

// --- Font-stack regression tests (font-framework plan Phase 3) --------------

TEST(WidgetLayoutTest, ScaledViewScalesFallbackGlyphsByOwnDesignSize)
{
    // Fallback glyphs (CJK) are rasterized at their own base size (e.g. 64)
    // while the primary base is 128. A 13px view must scale them by 13/64,
    // NOT by the primary 13/128 — otherwise a 64px CJK glyph renders at 6.5px.
    // FontManager is a process-wide singleton shared across every test suite;
    // drop any bases left by earlier suites (e.g. a real 16px bitmap base) so
    // findBestBase resolves the 13px (Bitmap flavor) request through THIS base.
    FontManager::get()->clearCache();
    auto base = std::make_shared<Font>();
    base->fontSize = 128.0f;
    // 13px resolves through a Bitmap base (kBitmapMaxSize = 48), so this
    // hand-built base must advertise Bitmap flavor — findBestBase only matches
    // bases of the requested flavor, otherwise a stale smaller bitmap base
    // would be selected instead of this one.
    base->renderMode = EFontRenderMode::Bitmap;
    base->lineHeight = 160.0f;
    base->ascent = 128.0f;
    base->descent = 32.0f;
    // Primary-style glyph captured at 128px.
    Character latin;
    latin.size       = {80, 100};
    latin.bearing    = {10, 100};
    latin.advance    = {100.0f, 0.0f};
    latin.designSize = 128;
    latin.atlasIndex = 0;
    base->characters['A'] = latin;
    // Fallback-style glyph captured at 64px (CJK).
    Character cjk;
    cjk.size       = {64, 64};
    cjk.bearing    = {0, 64};
    cjk.advance    = {64.0f, 0.0f};
    cjk.designSize = 64;
    cjk.atlasIndex = 1;
    base->characters[static_cast<uint32_t>(0x4F60)] = cjk;

    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 128, base);
    auto view = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isView());

    // Latin: 13/128 scale.
    const Character& vLatin = view->getCharacter('A');
    EXPECT_EQ(vLatin.advance.x, 100.0f * 13.0f / 128.0f);
    EXPECT_EQ(vLatin.size.x, std::lround(80.0f * 13.0f / 128.0f));
    // Fallback: 13/64 scale (the regression: 13/128 would halve it to 6.5).
    const Character& vCjk = view->getCharacter(static_cast<uint32_t>(0x4F60));
    EXPECT_FLOAT_EQ(vCjk.advance.x, 64.0f * 13.0f / 64.0f);
    EXPECT_EQ(vCjk.size.x, std::lround(64.0f * 13.0f / 64.0f));
    EXPECT_EQ(vCjk.size.y, std::lround(64.0f * 13.0f / 64.0f));
    EXPECT_EQ(vCjk.atlasIndex, 1u);
    FontManager::get()->clearCache();
}

TEST(WidgetLayoutTest, MeasureTextUsesResolvedFallbackGlyphAdvances)
{
    // After the stack resolves a CJK glyph, measureText must use its REAL
    // advance (13px at a 13px view), not the '?' fallback advance.
    // Isolate from any bases left by earlier suites (FontManager is shared).
    FontManager::get()->clearCache();
    auto base = std::make_shared<Font>();
    base->fontSize = 128.0f;
    base->renderMode = EFontRenderMode::Bitmap; // matches the 13px Bitmap request flavor
    base->lineHeight = 160.0f;
    base->ascent = 128.0f;
    base->descent = 32.0f;
    // '?' at 128px design: advance 80.
    Character question;
    question.size = {70, 100};
    question.bearing = {10, 100};
    question.advance = {80.0f, 0.0f};
    question.designSize = 128;
    base->characters['?'] = question;
    // CJK at 64px design: advance 64 (a 13px view should give 13px/char).
    Character cjk;
    cjk.size = {64, 64};
    cjk.bearing = {0, 64};
    cjk.advance = {64.0f, 0.0f};
    cjk.designSize = 64;
    base->characters[static_cast<uint32_t>(0x4F60)] = cjk;
    // Two CJK glyphs: text of 2 chars = 2 * 13px, NOT 2 * 8.125 (the '?'
    // advance at 13px would be 80*13/128 = 8.125).
    base->characters[static_cast<uint32_t>(0x597D)] = cjk; // 好

    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 128, base);
    auto view = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 13);
    ASSERT_NE(view, nullptr);
    const std::string text = "\xE4\xBD\xA0\xE5\xA5\xBD"; // 你好 (UTF-8)
    EXPECT_FLOAT_EQ(view->measureText(text), 2.0f * 64.0f * 13.0f / 64.0f);
    FontManager::get()->clearCache();
}

TEST(WidgetLayoutTest, BoxSlotFourSideMarginIsNotSymmetric)
{
    WidgetTree tree({.width = 200, .height = 80});
    auto box = std::make_shared<UIContainer>("Box");
    box->setSize({200.0f, 80.0f});
    box->setDirection(EWidgetBoxLayout::Horizontal);
    box->setPadding({0.0f, 0.0f});
    box->setSpacing(0.0f);
    tree.attachToLayer(WidgetTree::ELayer::Content, box);

    auto left = std::make_shared<UIPanel>("Left");
    left->setSize({40.0f, 20.0f});
    auto right = std::make_shared<UIPanel>("Right");
    right->setSize({40.0f, 20.0f});
    box->addDetachedChild(left, [](UIElement&, UISlot& slot) {
        if (auto* boxSlot = dynamic_cast<UIBoxSlot*>(&slot)) {
            boxSlot->setPreferredSize({40.0f, 20.0f});
        }
    });
    box->addDetachedChild(right, [](UIElement&, UISlot& slot) {
        if (auto* boxSlot = dynamic_cast<UIBoxSlot*>(&slot)) {
            boxSlot->setPreferredSize({40.0f, 20.0f});
        }
    });
    box->getBoxSlot(*right)->setMargin(FMargin{10.0f, 5.0f, 2.0f, 1.0f});

    tree.layout();
    EXPECT_FLOAT_EQ(left->_layoutRect.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(right->_layoutRect.pos.x, 50.0f); // 40 + left margin 10
    EXPECT_FLOAT_EQ(right->_layoutRect.pos.y, 5.0f);
    EXPECT_FLOAT_EQ(right->_layoutRect.extent.x, 40.0f);
}

TEST(WidgetLayoutTest, OverlaySlotAlignsWithoutChildAnchors)
{
    WidgetTree tree({.width = 200, .height = 100});
    auto overlay = std::make_shared<UIOverlay>("Host");
    overlay->setSize({200.0f, 100.0f});
    overlay->_bAutoSize = false;
    tree.attachToLayer(WidgetTree::ELayer::Content, overlay);

    auto fill = std::make_shared<UIPanel>("Fill");
    fill->setSize({10.0f, 10.0f});
    auto badge = std::make_shared<UIPanel>("Badge");
    badge->setSize({20.0f, 12.0f});
    overlay->addDetachedChild(fill, [](UIElement&, UISlot& slot) {
        if (auto* overlaySlot = dynamic_cast<UIOverlaySlot*>(&slot)) {
            overlaySlot->setPreferredSize({10.0f, 10.0f});
        }
    });
    overlay->addDetachedChild(badge, [](UIElement&, UISlot& slot) {
        if (auto* overlaySlot = dynamic_cast<UIOverlaySlot*>(&slot)) {
            overlaySlot->setPreferredSize({20.0f, 12.0f});
        }
    });
    overlay->getOverlaySlot(*badge)->apply(FOverlaySlotArgs{
        .hAlign  = EUIOverlayAlignment::End,
        .vAlign  = EUIOverlayAlignment::Start,
        .padding = FMargin::all(8.0f),
    });

    tree.layout();
    EXPECT_FLOAT_EQ(fill->_layoutRect.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.x, 200.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.y, 100.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.extent.x, 20.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.extent.y, 12.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.pos.x, 172.0f); // 8 + (184 - 20)
    EXPECT_FLOAT_EQ(badge->_layoutRect.pos.y, 8.0f);

    const auto dump = dumpWidgetTree(tree);
    const auto* hostNode = findWidgetNode(dump, "Host");
    const auto* badgeNode = findWidgetNode(dump, "Badge");
    ASSERT_NE(hostNode, nullptr);
    ASSERT_NE(badgeNode, nullptr);
    EXPECT_EQ((*hostNode)["layout"]["type"], "overlay");
    EXPECT_EQ((*badgeNode)["slot"]["type"], "overlay");
    EXPECT_EQ((*badgeNode)["slot"]["hAlign"], "end");
}

TEST(WidgetLayoutTest, SizeBoxPadsChildAndHonorsWidthOverride)
{
    WidgetTree tree({.width = 120, .height = 80});
    auto box = std::make_shared<UISizeBox>("Box");
    box->_bAutoSize = true;
    box->setPadding(FMargin{8.0f, 2.0f, 8.0f, 2.0f});
    box->setWidthOverride(40.0f);
    tree.attachToLayer(WidgetTree::ELayer::Content, box);

    auto child = std::make_shared<UIPanel>("Inner");
    child->setSize({10.0f, 10.0f});
    box->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UISingleChildSlot*>(&slot)) {
            single->setPreferredSize({10.0f, 10.0f});
        }
    });

    tree.layout();
    EXPECT_FLOAT_EQ(box->_layoutRect.extent.x, 40.0f);
    EXPECT_FLOAT_EQ(box->_layoutRect.extent.y, 14.0f); // 10 + 2 + 2
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x, 8.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y, 2.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 24.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 10.0f);
}

// === Child-stated intent: where a modifier lands ===
//
// "ui::panel().child(x).fillWidth()" is ambiguous on paper: child() returns the
// PARENT builder, so a modifier chained after it lands on the panel, not on the
// child. These cases pin that down so the DSL contract stays obvious.

// === Layout intent lives on the parent->child edge ===
//
// A child can no longer author its own stretch geometry (fillWidth() and friends
// are gone): intent is always a value on the edge, so the old ambiguity about
// "did this modifier land on the child or on the parent?" cannot occur.

TEST(WidgetLayoutTest, EdgeLayoutSpecAppliesToTheChildNotTheParent)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({400.0f, 200.0f})
                     [ui::layout().fill() >> ui::text("Label").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 400, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // The canvas host resolves the child from the edge, so it stretches.
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 400.0f);
    // The parent keeps its own authored size: intent never leaks upward.
    EXPECT_FLOAT_EQ(panel->_layoutRect.extent.x, 400.0f);
    EXPECT_FLOAT_EQ(panel->_layoutRect.extent.y, 200.0f);
}

// === Single-child slot intent (path-A hosts that own both axes) ===
//
// Scroll viewport / size box own both axes, so a child's own anchors are
// ignored. Intent is carried by the parent-child edge via singleChildSlot(),
// where Fill reproduces the historical stretch and align() opts out.

TEST(WidgetLayoutTest, SingleChildSlotDefaultsToFillReproducingStretch)
{
    registerSyntheticFont(16, 8.0f);

    auto box = ui::sizeBox("Box")
                   .setSize({200.0f, 100.0f})
                   .child(ui::text("Label").setText("Hi"))
                   .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, box);
    tree.layout();

    const UIElement* child = box->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // No slot args given: Fill/Fill must reproduce the pre-slot behaviour.
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 200.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 100.0f);
}

TEST(WidgetLayoutTest, SingleChildSlotAlignKeepsDesiredSizeAndCenters)
{
    registerSyntheticFont(16, 8.0f);

    auto box = ui::sizeBox("Box")
                   .setSize({200.0f, 100.0f})
                   .child(ui::text("Label").setText("Hi"),
                          ui::singleChildSlot().align(EUIOverlayAlignment::Center, EUIOverlayAlignment::Center))
                   .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, box);
    tree.layout();

    const UIElement* child = box->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // Desired size, not stretched: the slot is what makes this expressible.
    EXPECT_LT(child->_layoutRect.extent.x, 200.0f);
    EXPECT_LT(child->_layoutRect.extent.y, 100.0f);
    // Centred inside the 200x100 content box.
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x,
                    (200.0f - child->_layoutRect.extent.x) * 0.5f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y,
                    (100.0f - child->_layoutRect.extent.y) * 0.5f);
}

TEST(WidgetLayoutTest, UnifiedLayoutSpecAppliesToSingleChildSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto box = ui::sizeBox("Box")
                   .setSize({200.0f, 100.0f})
                   [ui::layout().align(EWidgetAlignH::Center, EWidgetAlignV::Center) >>
                    ui::text("Label").setText("Hi")]
                   .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, box);
    tree.layout();

    const UIElement* child = box->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UISingleChildSlot*>(box->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);

    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Center);
    EXPECT_LT(child->_layoutRect.extent.x, 200.0f);
    EXPECT_LT(child->_layoutRect.extent.y, 100.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x,
                    (200.0f - child->_layoutRect.extent.x) * 0.5f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y,
                    (100.0f - child->_layoutRect.extent.y) * 0.5f);
}

TEST(WidgetLayoutTest, UnifiedLayoutSpecAppliesToScrollViewportSingleChildSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto viewport = ui::scroll("Viewport")
                        .setSize({200.0f, 100.0f})
                        [ui::layout().align(EWidgetAlignH::Center, EWidgetAlignV::Top).size({40.0f, 160.0f}) >>
                         ui::text("Label").setText("Hi")]
                        .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, viewport);
    tree.layout();

    const UIElement* child = viewport->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UISingleChildSlot*>(viewport->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);

    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Start);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 40.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 160.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x, 80.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y, 0.0f);
}
// === Path A: intent belongs to the slot, not to the child's anchors ===
//
// A box container owns arrangement, so stretch anchors on its child would be
// dead code (and are rejected outright). The slot is the supported way to say
// "fill the main axis".

TEST(WidgetLayoutTest, PathAFillIsExpressedOnTheSlotNotTheChild)
{
    registerSyntheticFont(16, 8.0f);

    // "Fill" in a column means the main axis (Y): the text takes the space the
    // label does not.
    auto column = ui::column("Column")
                      .setSize({300.0f, 200.0f})
                      .child(ui::text("Label").setText("Hi"))
                      .child(ui::text("Filled").setText("Hi"), ui::boxSlot().fill())
                      .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    tree.layout();

    const auto children = column->getChildrenInPaintOrder();
    ASSERT_EQ(children.size(), 2u);
    const UIElement* label  = children[0];
    const UIElement* filled = children[1];

    ASSERT_NE(dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*filled)), nullptr);
    // The label keeps its desired height; the filled child absorbs the rest.
    EXPECT_LT(label->_layoutRect.extent.y, 200.0f);
    EXPECT_GT(filled->_layoutRect.extent.y, label->_layoutRect.extent.y);
    // Both stretch across the cross axis via the slot's default.
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 300.0f);
    EXPECT_FLOAT_EQ(filled->_layoutRect.extent.x, 300.0f);
}

TEST(WidgetLayoutTest, UnifiedLayoutSpecAppliesToOverlaySlot)
{
    auto overlay = ui::overlay("Host")
                       .setSize({200.0f, 100.0f})
                       [ui::layout().fill().size({10.0f, 10.0f}) >> ui::panel("Fill")]
                       [ui::layout().align(EWidgetAlignH::Right, EWidgetAlignV::Top).size({20.0f, 12.0f}) >>
                        ui::panel("Badge")]
                       .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, overlay);
    tree.layout();

    const auto children = overlay->getChildrenInPaintOrder();
    ASSERT_EQ(children.size(), 2u);
    const UIElement* fill = children[0];
    const UIElement* badge = children[1];
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(badge, nullptr);

    const auto* fillSlot = dynamic_cast<const UIOverlaySlot*>(overlay->getSlotForChild(*fill));
    const auto* badgeSlot = dynamic_cast<const UIOverlaySlot*>(overlay->getSlotForChild(*badge));
    ASSERT_NE(fillSlot, nullptr);
    ASSERT_NE(badgeSlot, nullptr);

    EXPECT_EQ(fillSlot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(fillSlot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(badgeSlot->getHAlign(), EUIOverlayAlignment::End);
    EXPECT_EQ(badgeSlot->getVAlign(), EUIOverlayAlignment::Start);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.x, 200.0f);
    EXPECT_FLOAT_EQ(fill->_layoutRect.extent.y, 100.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.extent.x, 20.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.extent.y, 12.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.pos.x, 180.0f);
    EXPECT_FLOAT_EQ(badge->_layoutRect.pos.y, 0.0f);
}

TEST(WidgetLayoutTest, CanvasPivotCentresAChildOnItsAnchoredPosition)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().anchor({0.5f, 0.5f}, {0.5f, 0.5f}).pivot({0.5f, 0.5f}).size({80.0f, 24.0f}) >>
                      ui::text("Inner").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // Centre of the child lands on the centre of the parent.
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x + child->_layoutRect.extent.x * 0.5f, 150.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y + child->_layoutRect.extent.y * 0.5f, 100.0f);
}

TEST(WidgetLayoutTest, CanvasPreferredSizeDrivesAnAutoAxis)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().widthSizeMode(EWidgetSizeMode::Auto)
                          .heightSizeMode(EWidgetSizeMode::Auto)
                          .preferredSize({123.0f, 45.0f}) >> ui::text("Inner").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // The slot's preferred size, not the measured text size, drives both axes.
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 123.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 45.0f);
}

TEST(WidgetLayoutTest, CanvasLayoutSpecSizeUsesTheSlotRatherThanMutatingChildGeometry)
{
    registerSyntheticFont(16, 8.0f);

    auto child = ui::text("Inner").setText("Hi").release();
    UIElement* const childRaw = child.get();
    ASSERT_NE(childRaw, nullptr);
    const glm::vec2 originalSize = childRaw->getSize();

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().size({120.0f, 32.0f}) >> child]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* liveChild = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(liveChild, nullptr);
    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*liveChild));
    ASSERT_NE(slot, nullptr);

    EXPECT_EQ(slot->getFixedSize(), glm::vec2(120.0f, 32.0f));
    EXPECT_EQ(childRaw->getSize(), originalSize)
        << "canvas layout intent must stay on the slot, not overwrite child geometry";
    EXPECT_FLOAT_EQ(liveChild->_layoutRect.extent.x, 120.0f);
    EXPECT_FLOAT_EQ(liveChild->_layoutRect.extent.y, 32.0f);
}

TEST(WidgetLayoutTest, StretchXFixedHeightChromeLivesOnTheCanvasSlot)
{
    auto host = ui::panel("Host").setSize({400.0f, 300.0f}).release();
    auto bar  = ui::panel("Bar").release();

    WidgetTree tree({.width = 400, .height = 300});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, host).valid());
    host->addDetachedChild(bar);
    ui::attachLayout(*host, *bar,
                    ui::layout().anchor({0.0f, 0.0f}, {1.0f, 0.0f}).size({0.0f, 30.0f}).args());
    tree.layout();

    const auto* slot = dynamic_cast<const UICanvasSlot*>(host->getSlotForChild(*bar));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(0.0f, 30.0f));
    EXPECT_FLOAT_EQ(bar->_layoutRect.extent.x, 400.0f);
    EXPECT_FLOAT_EQ(bar->_layoutRect.extent.y, 30.0f);
}

TEST(WidgetLayoutTest, CanvasSlotOffsetAndFixedSizeCanBeUpdatedAfterAttach)
{
    auto host  = ui::panel("Host").setSize({400.0f, 300.0f}).release();
    auto child = ui::panel("Child").release();

    WidgetTree tree({.width = 400, .height = 300});
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, host).valid());
    host->addDetachedChild(child);
    ui::attachLayout(*host, *child,
                    ui::layout()
                        .anchor({0.5f, 0.5f}, {0.5f, 0.5f})
                        .offsets({-70.0f, -45.0f})
                        .size({140.0f, 90.0f})
                        .args());

    auto* slot = dynamic_cast<UICanvasSlot*>(host->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(-70.0f, -45.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(140.0f, 90.0f));

    const glm::vec2 nextSize{180.0f, 120.0f};
    slot->setFixedSize(nextSize);
    slot->setOffset(-nextSize * 0.5f);
    tree.layout();

    EXPECT_EQ(slot->getOffset(), glm::vec2(-90.0f, -60.0f));
    EXPECT_EQ(slot->getFixedSize(), nextSize);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 180.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 120.0f);
}

TEST(WidgetLayoutTest, ExplicitCanvasAttachDoesNotMutateChildGeometry)
{
    auto child = ui::panel("Child").release();

    child->setPosition({11.0f, 22.0f});
    child->setSize({33.0f, 44.0f});
    child->_anchorMin = {0.1f, 0.2f};
    child->_anchorMax = {0.3f, 0.4f};

    WidgetTree tree({.width = 400, .height = 300});

    FCanvasSlotArgs args;
    args.anchorMin = {0.0f, 0.0f};
    args.anchorMax = {1.0f, 0.0f};
    args.offset    = {80.0f, 12.0f};
    args.fixedSize = {200.0f, 30.0f};

    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, child, args).valid());

    EXPECT_EQ(child->getPosition(), glm::vec2(11.0f, 22.0f));
    EXPECT_EQ(child->getSize(), glm::vec2(33.0f, 44.0f));
    EXPECT_EQ(child->_anchorMin, glm::vec2(0.1f, 0.2f));
    EXPECT_EQ(child->_anchorMax, glm::vec2(0.3f, 0.4f));

    auto* slot = dynamic_cast<UICanvasSlot*>(tree.getLayer(WidgetTree::ELayer::Content)->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), args.anchorMin);
    EXPECT_EQ(slot->getAnchorMax(), args.anchorMax);
    EXPECT_EQ(slot->getOffset(), args.offset);
    EXPECT_EQ(slot->getFixedSize(), args.fixedSize);
}

TEST(WidgetLayoutTest, DeclarativeBracketAndAttachLayoutProduceEquivalentCanvasSlots)
{
    const auto specBuilder = ui::layout()
                                 .anchor({0.25f, 0.5f}, {0.25f, 0.5f})
                                 .offsets(glm::vec2{11.0f, 13.0f})
                                 .offsets(2.0f, 4.0f, 6.0f, 8.0f)
                                 .align(EWidgetAlignH::Center, EWidgetAlignV::Bottom)
                                 .widthSizeMode(EWidgetSizeMode::Auto)
                                 .heightSizeMode(EWidgetSizeMode::Fixed)
                                 .pivot({0.5f, 1.0f})
                                 .size({70.0f, 30.0f});
    const FUILayoutSpec spec = specBuilder.args();

    auto declarative = ui::panel("Declarative")
                           [specBuilder >> ui::text("DeclarativeChild").setText("Hi")]
                           .release();
    auto imperative      = ui::panel("Imperative").release();
    auto imperativeChild = ui::text("ImperativeChild").setText("Hi").release();
    imperative->addDetachedChild(imperativeChild);
    ui::attachLayout(*imperative, *imperativeChild, spec);

    ASSERT_EQ(declarative->getChildren().size(), 1u);
    const UIElement* declarativeChild = declarative->getChildren()[0].get();
    ASSERT_NE(declarativeChild, nullptr);
    const auto* declarativeSlot = dynamic_cast<const UICanvasSlot*>(declarative->getSlotForChild(*declarativeChild));
    const auto* imperativeSlot  = dynamic_cast<const UICanvasSlot*>(imperative->getSlotForChild(*imperativeChild));
    ASSERT_NE(declarativeSlot, nullptr);
    ASSERT_NE(imperativeSlot, nullptr);

    EXPECT_EQ(declarativeSlot->getAnchorMin(), imperativeSlot->getAnchorMin());
    EXPECT_EQ(declarativeSlot->getAnchorMax(), imperativeSlot->getAnchorMax());
    EXPECT_EQ(declarativeSlot->getOffset(), imperativeSlot->getOffset());
    EXPECT_EQ(declarativeSlot->getOffsets(), imperativeSlot->getOffsets());
    EXPECT_EQ(declarativeSlot->getAlignmentH(), imperativeSlot->getAlignmentH());
    EXPECT_EQ(declarativeSlot->getAlignmentV(), imperativeSlot->getAlignmentV());
    EXPECT_EQ(declarativeSlot->getWidthSizeMode(), imperativeSlot->getWidthSizeMode());
    EXPECT_EQ(declarativeSlot->getHeightSizeMode(), imperativeSlot->getHeightSizeMode());
    EXPECT_EQ(declarativeSlot->getPivot(), imperativeSlot->getPivot());
    EXPECT_EQ(declarativeSlot->getFixedSize(), imperativeSlot->getFixedSize());
}

TEST(WidgetLayoutTest, BuildWithLayoutSpecInitializesTheCanvasSlot)
{
    WidgetTree tree({.width = 300, .height = 200});
    auto       panel = ui::panel("Panel").setSize({300.0f, 200.0f}).release();
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panel).valid());

    const FUILayoutSpec spec = ui::layout()
                                   .anchor({0.5f, 0.5f}, {0.5f, 0.5f})
                                   .pivot({0.5f, 0.5f})
                                   .size({80.0f, 24.0f})
                                   .args();
    const UIElementRef child = ui::build(tree, *panel, ui::text("BuiltChild").setText("Hi"), spec);
    ASSERT_NE(child, nullptr);

    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getPivot(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(80.0f, 24.0f));
}

TEST(WidgetLayoutTest, CanvasHostSetSizeBridgesToTheCanvasSlotFixedSize)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel").setSize({300.0f, 200.0f}).release();
    auto child = ui::text("Inner").setText("Hi").release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    panel->addDetachedChild(child);

    child->setSize({90.0f, 28.0f});
    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(90.0f, 28.0f));

    tree.layout();
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 90.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 28.0f);
}

TEST(WidgetLayoutTest, CanvasHostIsNotBoundToThePanelVisuals)
{
    registerSyntheticFont(16, 8.0f);

    // ui::canvas() carries the anchor layout without a panel's own visuals.
    auto host = ui::canvas("Host")
                    .setSize({200.0f, 100.0f})
                    [ui::layout().fill() >> ui::text("Inner").setText("Hi")]
                    .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, host);
    tree.layout();

    const UIElement* child = host->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(host->_styleKey, "canvas");
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 200.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 100.0f);
}

// === Reparent / detach rebuilds the slot edge ===
//
// The slot belongs to the parent->child EDGE, not to the child. Moving a child
// between hosts must therefore rebuild it with the new host's slot type, and
// detaching must leave the child with no slot at all.

TEST(WidgetLayoutTest, ReparentingBetweenHostsRebuildsTheSlotForTheNewHost)
{
    registerSyntheticFont(16, 8.0f);

    WidgetTree tree({.width = 200, .height = 100});
    auto       panelHost = ui::panel("PanelHost").setSize({200.0f, 100.0f}).release();
    auto       boxHost   = ui::column("BoxHost").setSize({200.0f, 100.0f}).release();
    auto       child     = ui::text("Child").setText("Hi").setSize({50.0f, 20.0f}).release();

    // Both hosts must be in the tree before the child is attached, so that
    // reparent() has a target the tree owns.
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panelHost).valid());
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, boxHost).valid());
    panelHost->addDetachedChild(child);

    // Under a canvas host the edge is a canvas slot.
    ASSERT_NE(dynamic_cast<UICanvasSlot*>(panelHost->getSlotForChild(*child)), nullptr);
    ASSERT_EQ(dynamic_cast<UIBoxSlot*>(panelHost->getSlotForChild(*child)), nullptr);

    // Move it to a box host: the edge must be rebuilt as a box slot and the old
    // canvas edge must be gone.
    tree.reparent(*boxHost, child);
    EXPECT_EQ(panelHost->getSlotForChild(*child), nullptr) << "old host keeps no edge";
    EXPECT_NE(dynamic_cast<UIBoxSlot*>(child->getSlot()), nullptr);
    EXPECT_EQ(dynamic_cast<UICanvasSlot*>(child->getSlot()), nullptr);

    // Detaching entirely leaves the child with no edge at all.
    tree.detach(*child);
    EXPECT_EQ(child->getSlot(), nullptr);
}

TEST(WidgetLayoutTest, ReparentingAcrossHostsDoesNotLeakTheOldHostIntent)
{
    registerSyntheticFont(16, 8.0f);

    WidgetTree tree({.width = 200, .height = 100});
    auto       panelHost = ui::panel("PanelHost").setSize({200.0f, 100.0f}).release();
    auto       boxHost   = ui::column("BoxHost").setSize({200.0f, 100.0f}).release();
    auto       child     = ui::text("Child").setText("Hi").setSize({50.0f, 20.0f}).release();

    // Anchor intent is a canvas capability and applies while under the panel.
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, panelHost).valid());
    ASSERT_TRUE(tree.attachToLayer(WidgetTree::ELayer::Content, boxHost).valid());
    panelHost->addDetachedChild(child);
    if (auto* slot = dynamic_cast<UICanvasSlot*>(panelHost->getSlotForChild(*child))) {
        slot->apply(fillArgs);
    }
    tree.layout();
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 200.0f) << "canvas host fills the child";

    // The old canvas fill gave the child the parent's full height (100). After
    // moving to a box host that intent must be gone: the height comes from the
    // child's own size (20). The width follows the box host's own cross-axis
    // stretch, which is box behaviour, not leaked anchor intent.
    tree.reparent(*boxHost, child);
    tree.layout();
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 20.0f)
        << "the old canvas fill intent must not leak into the new box edge";
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 200.0f)
        << "width follows the box host's cross-axis stretch, not the old anchor";
}

// === Capability isolation (compile time) ===
//
// Layout intent is a capability set carried in the TYPE, so a host that cannot
// honour an intent rejects it while compiling instead of silently dropping it.
// These are the checks the plan requires; they must be static_asserts because
// the wrong code must fail to build, not fail at runtime.

// A box host shares space along the main axis: it honours grow but not anchors
// or grid cells.
static_assert(LayoutCapsCompatible<EUILayoutCap::Grow, kBoxHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Anchor, kBoxHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Cell, kBoxHostCaps>);

// A canvas host positions by anchor rects and edge insets: it honours anchors
// but not main-axis sharing or grid cells.
static_assert(LayoutCapsCompatible<EUILayoutCap::Anchor, kCanvasHostCaps>);
static_assert(LayoutCapsCompatible<EUILayoutCap::Offsets, kCanvasHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Grow, kCanvasHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Cell, kCanvasHostCaps>);

// A grid host places children in cells and nothing else positional.
static_assert(LayoutCapsCompatible<EUILayoutCap::Cell, kGridHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Grow, kGridHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Anchor, kGridHostCaps>);
static_assert(!LayoutCapsCompatible<EUILayoutCap::Align, kGridHostCaps>);

// Fill / alignment / spacing are understood everywhere.
static_assert(LayoutCapsCompatible<EUILayoutCap::Fill, kBoxHostCaps>);
static_assert(LayoutCapsCompatible<EUILayoutCap::Fill, kCanvasHostCaps>);
static_assert(LayoutCapsCompatible<EUILayoutCap::Align, kSingleChildHostCaps>);

// The builder type carries the union of every capability applied to it, so the
// host check sees the whole intent (not just the last modifier).
static_assert(decltype(ui::layout().grow(1.0f))::caps() == EUILayoutCap::Grow);
static_assert(decltype(ui::layout().anchor({0.0f, 0.0f}, {1.0f, 1.0f}))::caps() ==
              EUILayoutCap::Anchor);
static_assert(decltype(ui::layout().cell(0, 1))::caps() == EUILayoutCap::Cell);
static_assert((decltype(ui::layout().fill().align(EWidgetAlignH::Center))::caps() &
               EUILayoutCap::Fill) != EUILayoutCap::None);
static_assert((decltype(ui::layout().fill().align(EWidgetAlignH::Center))::caps() &
               EUILayoutCap::Align) != EUILayoutCap::None);

// The concrete rejections the plan calls out: an anchor intent cannot be
// attached to a column (box host), and a grow intent cannot be attached to a
// panel (canvas host).
// The same check bound to the actual host types: operator[] is constrained by
// LayoutCapsCompatible against the host's declared set, so these are exactly the
// accept/reject decisions the compiler makes at every attach site. (The
// end-to-end proof is that `column[ui::layout().anchor(...) >> w]` fails to
// build - it was caught in WorkbenchDemoPages and had to be expressed in box
// terms instead.)
static_assert(LayoutCapsCompatible<ya::EUILayoutCap::Grow, ui::UIContainerWidgetBuilder::kAllowedLayoutCaps>);
static_assert(!LayoutCapsCompatible<ya::EUILayoutCap::Anchor, ui::UIContainerWidgetBuilder::kAllowedLayoutCaps>);
static_assert(!LayoutCapsCompatible<ya::EUILayoutCap::Cell, ui::UIContainerWidgetBuilder::kAllowedLayoutCaps>);
static_assert(LayoutCapsCompatible<ya::EUILayoutCap::Anchor, ui::UIPanelWidgetBuilder::kAllowedLayoutCaps>);
static_assert(LayoutCapsCompatible<ya::EUILayoutCap::Offsets, ui::UIPanelWidgetBuilder::kAllowedLayoutCaps>);
static_assert(!LayoutCapsCompatible<ya::EUILayoutCap::Grow, ui::UIPanelWidgetBuilder::kAllowedLayoutCaps>);
static_assert(!LayoutCapsCompatible<ya::EUILayoutCap::Cell, ui::UIPanelWidgetBuilder::kAllowedLayoutCaps>);

// === Canvas layout capabilities ===
//
// Canvas is an ordinary layout a host installs; these cover the capabilities its
// slot carries: per-edge insets, per-axis size mode and alignment.

TEST(WidgetLayoutTest, CanvasFourSideOffsetsInsetTheChildWithoutAnExplicitSize)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().fill().offsets(10.0f, 20.0f, 30.0f, 40.0f) >> ui::text("Inner").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // Fill minus insets: no hand-computed size is needed.
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 300.0f - 10.0f - 30.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 200.0f - 20.0f - 40.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x, 10.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y, 20.0f);
}

TEST(WidgetLayoutTest, CanvasAutoSizeModeUsesMeasuredContentNotTheParent)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().fill().widthSizeMode(EWidgetSizeMode::Auto) >>
                      ui::text("Inner").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // Width follows the content (SizeToContent), height still stretches.
    EXPECT_LT(child->_layoutRect.extent.x, 300.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 200.0f);
}

TEST(WidgetLayoutTest, CanvasAlignmentPlacesAFixedSizeChildInsideTheArea)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel")
                     .setSize({300.0f, 200.0f})
                     [ui::layout().align(EWidgetAlignH::Center, EWidgetAlignV::Bottom).size({80.0f, 24.0f}) >>
                      ui::text("Inner").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.layout();

    const UIElement* child = panel->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    // Fixed size is preserved, then placed inside the parent's area.
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 80.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 24.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x, (300.0f - 80.0f) * 0.5f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y, 200.0f - 24.0f);
}

TEST(WidgetLayoutTest, BoxHostSetSizeBridgesToTheBoxSlotPreferredSize)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column").setSize({240.0f, 100.0f}).release();
    auto child  = ui::text("Inner").setText("Hi").release();

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    column->addDetachedChild(child);

    child->setSize({90.0f, 28.0f});
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 28.0f));

    tree.layout();
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 28.0f);
}

TEST(WidgetLayoutTest, SelectableRowArrangesLabelThroughTheSingleChildSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto row = std::make_shared<UISelectableRow>("Row");
    row->setSize({240.0f, 22.0f});
    row->setContentPadding(FMargin{28.0f, 0.0f, 0.0f, 0.0f});
    auto label = std::make_shared<UIText>("Label");
    label->setText("Item");
    label->_fontSize = 13;
    label->_vAlign   = EWidgetAlignV::Center;

    WidgetTree tree({.width = 400, .height = 300});
    tree.attachToLayer(WidgetTree::ELayer::Content, row);
    tree.attach(*row, label);
    tree.layout();

    const auto* slot = dynamic_cast<const UISingleChildSlot*>(row->getSlotForChild(*label));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_FLOAT_EQ(label->_layoutRect.pos.x, 28.0f);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 212.0f);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.y, 22.0f);

    const auto dump = dumpWidgetTree(tree);
    const auto* rowNode = findWidgetNode(dump, "Row");
    ASSERT_NE(rowNode, nullptr);
    EXPECT_EQ((*rowNode)["layout"]["type"], "singleChild");
    EXPECT_EQ((*rowNode)["layout"]["padding"]["left"], 28.0f);
}

TEST(WidgetLayoutTest, BuildWithLayoutAttachmentInitializesTheBoxSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column").setSize({240.0f, 100.0f}).release();
    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);

    ui::build(tree,
              *column,
              ui::layout().size({0.0f, 22.0f}) >> ui::selectableRow("Row").setItemId("a"));
    tree.layout();

    ASSERT_FALSE(column->getChildrenInPaintOrder().empty());
    UIElement* row = column->getChildrenInPaintOrder().front();
    ASSERT_NE(row, nullptr);
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*row));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(0.0f, 22.0f));
    EXPECT_FLOAT_EQ(row->_layoutRect.extent.y, 22.0f);
    EXPECT_NE(row->getSize(), glm::vec2(0.0f, 22.0f))
        << "row height must live on the parent box slot, not the child";
}

TEST(WidgetLayoutTest, UnifiedLayoutSpecAppliesToSelectableRowSingleChildSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto row = ui::selectableRow("Row")
                     .setSize({200.0f, 40.0f})
                     [ui::layout().align(EWidgetAlignH::Center, EWidgetAlignV::Center).size({40.0f, 16.0f}) >>
                      ui::text("Label").setText("Hi")]
                     .release();

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, row);
    tree.layout();

    const UIElement* child = row->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UISingleChildSlot*>(row->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Center);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Center);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 40.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 16.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.x, 80.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.pos.y, 12.0f);
}

TEST(WidgetLayoutTest, CheckBoxArrangesLabelThroughTheSingleChildSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto box = std::make_shared<UICheckBox>("Check");
    box->setSize({200.0f, 24.0f});
    auto label = std::make_shared<UIText>("Label");
    label->setText("On");
    label->_fontSize = 13;

    WidgetTree tree({.width = 400, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, box);
    tree.attach(*box, label);
    tree.layout();

    const auto* slot = dynamic_cast<const UISingleChildSlot*>(box->getSlotForChild(*label));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_FLOAT_EQ(label->_layoutRect.pos.x, 24.0f);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.x, 176.0f);
    EXPECT_FLOAT_EQ(label->_layoutRect.extent.y, 24.0f);
}

TEST(WidgetLayoutTest, BoxSlotArgsPreferredSizeLivesOnTheEdge)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column")
                       .setSize({240.0f, 100.0f})
                       .child(ui::text("Inner").setText("Hi"),
                              FBoxSlotArgs{.preferredSize = {90.0f, 28.0f}})
                       .release();

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    tree.layout();

    const UIElement* child = column->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 28.0f));
    EXPECT_NE(child->getSize(), glm::vec2(90.0f, 28.0f))
        << "box preferred size must live on the slot, not the child";
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 28.0f);
}

TEST(WidgetLayoutTest, AttachSeedsAuthoredChildSizeOntoTheBoxSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column")
                       .setSize({240.0f, 100.0f})
                       .child(ui::text("Inner").setText("Hi"), ui::boxSlot().preferredSize({90.0f, 28.0f}))
                       .release();

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    tree.layout();

    const UIElement* child = column->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 28.0f));
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 28.0f);
}

TEST(WidgetLayoutTest, AttachDoesNotSeedDefaultChildSizeOntoTheBoxSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column").setSize({240.0f, 100.0f}).release();
    auto child  = ui::text("Inner").setText("Hi").release();

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    column->addDetachedChild(child);

    EXPECT_FALSE(child->hasAuthoredSize());
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(0.0f, 0.0f));
}

TEST(WidgetLayoutTest, AttachSeedsAuthoredChildGeometryOntoTheCanvasSlot)
{
    registerSyntheticFont(16, 8.0f);

    auto panel = ui::panel("Panel").setSize({300.0f, 200.0f}).release();
    auto child = ui::text("Inner").setText("Hi").setPosition({12.0f, 8.0f}).setSize({80.0f, 24.0f}).release();

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    panel->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            FCanvasSlotArgs args;
            args.offset    = {12.0f, 8.0f};
            args.fixedSize = {80.0f, 24.0f};
            canvas->apply(args);
        }
    });

    EXPECT_TRUE(child->hasAuthoredPosition());
    EXPECT_TRUE(child->hasAuthoredSize());
    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(12.0f, 8.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(80.0f, 24.0f));

    tree.layout();
    EXPECT_EQ(child->_layoutRect.pos, glm::vec2(12.0f, 8.0f));
    EXPECT_EQ(child->_layoutRect.extent, glm::vec2(80.0f, 24.0f));
}

TEST(WidgetLayoutTest, LayoutSpecPreferredSizeWinsOverAuthoredChildSize)
{
    registerSyntheticFont(16, 8.0f);

    auto column = ui::column("Column")
                       .setSize({240.0f, 100.0f})
                       [ui::layout().size({0.0f, 22.0f}) >> ui::text("Inner").setText("Hi").setSize({90.0f, 28.0f})]
                       .release();

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    tree.layout();

    const UIElement* child = column->getChildrenInPaintOrder().front();
    ASSERT_NE(child, nullptr);
    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(0.0f, 22.0f));
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 22.0f);
}

TEST(WidgetLayoutTest, BoxLayoutIgnoresCorruptedChildSizeAfterAttach)
{
    auto column = ui::column("Column").setSize({240.0f, 100.0f}).release();
    auto child  = std::make_shared<FSizeProbe>("Inner");
    child->setSize({90.0f, 28.0f});

    WidgetTree tree({.width = 240, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, column);
    column->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({90.0f, 28.0f});
        }
    });

    const auto* slot = dynamic_cast<const UIBoxSlot*>(column->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 28.0f));

    child->corruptSize({1.0f, 1.0f});
    tree.invalidateLayout();
    tree.layout();
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(90.0f, 28.0f));
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.y, 28.0f);
    EXPECT_FLOAT_EQ(child->_layoutRect.extent.x, 240.0f);
}

TEST(WidgetLayoutTest, CanvasLayoutIgnoresCorruptedChildSizeAfterAttach)
{
    auto panel = ui::panel("Panel").setSize({300.0f, 200.0f}).release();
    auto child = std::make_shared<FSizeProbe>("Inner");

    WidgetTree tree({.width = 300, .height = 200});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    panel->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            canvas->setFixedSize({80.0f, 24.0f});
        }
    });

    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(80.0f, 24.0f));

    child->corruptSize({1.0f, 1.0f});
    tree.invalidateLayout();
    tree.layout();
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(80.0f, 24.0f));
    EXPECT_EQ(child->_layoutRect.extent, glm::vec2(80.0f, 24.0f));
}

TEST(WidgetLayoutTest, OverlayLayoutIgnoresCorruptedChildSizeAfterAttach)
{
    auto overlay = std::make_shared<UIOverlay>("Host");
    overlay->setSize({200.0f, 100.0f});
    overlay->_bAutoSize = false;

    auto badge = std::make_shared<FSizeProbe>("Badge");

    WidgetTree tree({.width = 200, .height = 100});
    tree.attachToLayer(WidgetTree::ELayer::Content, overlay);
    overlay->addDetachedChild(badge, [](UIElement&, UISlot& slot) {
        if (auto* overlaySlot = dynamic_cast<UIOverlaySlot*>(&slot)) {
            overlaySlot->apply(FOverlaySlotArgs{
                .hAlign = EUIOverlayAlignment::End,
                .vAlign = EUIOverlayAlignment::Start,
                .preferredSize = {20.0f, 12.0f},
            });
        }
    });

    const auto* slot = dynamic_cast<const UIOverlaySlot*>(overlay->getSlotForChild(*badge));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(20.0f, 12.0f));

    badge->corruptSize({1.0f, 1.0f});
    tree.invalidateLayout();
    tree.layout();
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(20.0f, 12.0f));
    EXPECT_EQ(badge->_layoutRect.extent, glm::vec2(20.0f, 12.0f));
}

TEST(WidgetLayoutTest, DockSpaceArrangesProjectionThroughTheSingleChildSlot)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       ws   = std::make_shared<UIDockWorkspace>();
    auto       dock = std::make_shared<UIDockSpace>("Dock");
    dock->_anchorMin = {0.0f, 0.0f};
    dock->_anchorMax = {1.0f, 1.0f};
    dock->setWorkspace(ws);
    tree.attachToLayer(WidgetTree::ELayer::Content, dock);

    auto panel = std::make_shared<UIPanel>("SceneBody");
    ASSERT_NE(ws->addPanel("Scene", panel), kInvalidDockPanelId);
    tree.layout();

    ASSERT_FALSE(dock->getChildren().empty());
    const UIElement* projection = dock->getChildren().front().get();
    ASSERT_NE(projection, nullptr);
    const auto* slot = dynamic_cast<const UISingleChildSlot*>(dock->getSlotForChild(*projection));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(slot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(projection->_layoutRect.pos, dock->_layoutRect.pos);
    EXPECT_EQ(projection->_layoutRect.extent, dock->_layoutRect.extent);
}

TEST(WidgetLayoutTest, FloatingWindowGeometryLivesOnTheHostCanvasSlot)
{
    WidgetTree tree({.width = 1000, .height = 700});
    auto       ws   = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;

    auto host = std::make_shared<UIDockFloatingHost>("Host");
    host->bindWorkspace(ws);
    FCanvasSlotArgs hostFill;
    hostFill.anchorMin = {0.0f, 0.0f};
    hostFill.anchorMax = {1.0f, 1.0f};
    tree.attachToLayer(WidgetTree::ELayer::Popup, host, hostFill);

    auto             panel     = std::make_shared<UIPanel>("SceneBody");
    const DockPanelId panelId  = ws->addPanel("Scene", panel);
    const FDockFloatingWindowId floatingId =
        ws->tearOffPanel(panelId, {120.0f, 80.0f}, {320.0f, 240.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    host->syncFromWorkspace();
    tree.layout();

    ASSERT_FALSE(host->getChildren().empty());
    UIElement* window = host->getChildren().front().get();
    ASSERT_NE(window, nullptr);
    const auto* slot = dynamic_cast<const UICanvasSlot*>(host->getSlotForChild(*window));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(120.0f, 80.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(320.0f, 240.0f));
    EXPECT_EQ(window->_layoutRect.pos, glm::vec2(120.0f, 80.0f));
    EXPECT_EQ(window->_layoutRect.extent, glm::vec2(320.0f, 240.0f));
}

TEST(WidgetLayoutTest, FloatingWindowResizeHandlesLiveOnOverlaySlots)
{
    WidgetTree tree({.width = 1000, .height = 700});
    auto       ws = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;

    auto host = std::make_shared<UIDockFloatingHost>("Host");
    host->bindWorkspace(ws);
    FCanvasSlotArgs hostFill;
    hostFill.anchorMin = {0.0f, 0.0f};
    hostFill.anchorMax = {1.0f, 1.0f};
    tree.attachToLayer(WidgetTree::ELayer::Popup, host, hostFill);

    auto                  panel    = std::make_shared<UIPanel>("SceneBody");
    const DockPanelId     panelId  = ws->addPanel("Scene", panel);
    ASSERT_NE(ws->tearOffPanel(panelId, {100.0f, 80.0f}, {300.0f, 200.0f}), kInvalidFloatingWindowId);
    host->syncFromWorkspace();
    tree.layout();

    ASSERT_FALSE(host->getChildren().empty());
    UIElement* window = host->getChildren().front().get();
    ASSERT_NE(window, nullptr);
    const auto& children = window->getChildren();
    ASSERT_GE(children.size(), 6u);

    const UIElement* chrome = children[0].get();
    const auto* chromeSlot = dynamic_cast<const UIOverlaySlot*>(window->getSlotForChild(*chrome));
    ASSERT_NE(chromeSlot, nullptr);
    EXPECT_EQ(chromeSlot->getHAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(chromeSlot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_EQ(chrome->_layoutRect.pos, window->_layoutRect.pos);
    EXPECT_EQ(chrome->_layoutRect.extent, window->_layoutRect.extent);

    const UIElement* left = children[1].get();
    const auto* leftSlot = dynamic_cast<const UIOverlaySlot*>(window->getSlotForChild(*left));
    ASSERT_NE(leftSlot, nullptr);
    EXPECT_EQ(leftSlot->getHAlign(), EUIOverlayAlignment::Start);
    EXPECT_EQ(leftSlot->getVAlign(), EUIOverlayAlignment::Fill);
    EXPECT_FLOAT_EQ(left->_layoutRect.pos.x, window->_layoutRect.pos.x);
    EXPECT_FLOAT_EQ(left->_layoutRect.extent.x, 6.0f);
    EXPECT_FLOAT_EQ(left->_layoutRect.extent.y, window->_layoutRect.extent.y);

    const UIElement* corner = children.back().get();
    const auto* cornerSlot = dynamic_cast<const UIOverlaySlot*>(window->getSlotForChild(*corner));
    ASSERT_NE(cornerSlot, nullptr);
    EXPECT_EQ(cornerSlot->getHAlign(), EUIOverlayAlignment::End);
    EXPECT_EQ(cornerSlot->getVAlign(), EUIOverlayAlignment::End);
    EXPECT_FLOAT_EQ(corner->_layoutRect.extent.x, 14.0f);
    EXPECT_FLOAT_EQ(corner->_layoutRect.extent.y, 14.0f);
}
} // namespace ya
