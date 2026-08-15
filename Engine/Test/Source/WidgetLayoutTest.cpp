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
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Resources/FontManager.h"

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

TEST(WidgetLayoutTest, TextWithoutAutoSizeKeepsExplicitSize)
{
    registerSyntheticFont(16, 8.0f);
    auto label = std::make_shared<UIText>("Fixed");
    label->setText("Hello");
    label->_fontSize = 16;
    label->setSize({120.0f, 24.0f});
    EXPECT_EQ(label->computeDesiredSize(), glm::vec2(120.0f, 24.0f));
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

TEST(WidgetLayoutTest, EmptyAutoButtonKeepsExplicitFallbackSize)
{
    WidgetTree tree({.width = 400, .height = 200});
    auto button = std::make_shared<UIButton>("Empty");
    button->_bAutoSize = true;
    button->setSize({91.0f, 37.0f});
    button->setContentPadding({12.0f, 6.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
    tree.layout();

    EXPECT_EQ(button->computeDesiredSize(), glm::vec2(91.0f, 37.0f));
    EXPECT_EQ(button->_layoutRect.extent, glm::vec2(91.0f, 37.0f));
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
    tree.attach(*row, grow);
    tree.attach(*row, shrink);
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
    tree.attach(*hbox, spacer);
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
    tree.attach(*box, fixed);
    tree.attach(*box, fill);
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
    EXPECT_EQ(childSlot->getMargin(), glm::vec2(3.0f, 2.0f));

    tree.reparent(*outer, child);
    auto* reparentedSlot = outer->getBoxSlot(*child);
    ASSERT_NE(reparentedSlot, nullptr);
    EXPECT_EQ(child->getSlot(), reparentedSlot);
    EXPECT_EQ(&reparentedSlot->getParent(), outer.get());
    EXPECT_EQ(inner->getBoxSlot(*child), nullptr);
    EXPECT_EQ(reparentedSlot->getSizeRule(), EUIBoxSlotSizeRule::Auto);
    EXPECT_EQ(reparentedSlot->getMargin(), glm::vec2(0.0f, 0.0f));

    reparentedSlot->setSizeRule(EUIBoxSlotSizeRule::Fill);
    reparentedSlot->setWeight(1.0f);
    tree.layout();
    const auto dump = dumpWidgetTree(tree);
    const auto* childNode = findWidgetNode(dump, "Child");
    ASSERT_NE(childNode, nullptr);
    EXPECT_EQ((*childNode)["slot"]["parent"], "Outer");
    EXPECT_EQ((*childNode)["slot"]["sizeRule"], "fill");
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

} // namespace ya
