// GAH-004: CPU-only cost of representative WidgetTree scenes. Records draw
// items, compose flush, paint, and arrange for C1/C2 comparison. Does not
// change clip-run batching or layout skip.

#include "GUI/Compose/UIFrameComposeReplay.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/ColorEdit.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include "Core/Event.h"

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <string>

namespace ya
{
namespace
{

struct FSceneCost
{
    uint32_t drawItems       = 0;
    uint32_t paintedWidgets  = 0;
    uint32_t rebuiltWidgets  = 0;
    uint32_t arrangeCount    = 0;
    float    layoutMS        = 0.0f;
    float    paintMS         = 0.0f;
    FUIFrameComposeReplayStats compose;
};

std::shared_ptr<Font> registerBaselineFont()
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

FCanvasSlotArgs fillCanvas()
{
    FCanvasSlotArgs args;
    args.anchorMin = {0.0f, 0.0f};
    args.anchorMax = {1.0f, 1.0f};
    return args;
}

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

FSceneCost measureScene(WidgetTree& tree, UILayout* layout)
{
    if (layout != nullptr) {
        layout->resetArrangeCount();
    }
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    const GuiPerfStats    perf = tree.getPerfStats();

    FSceneCost cost;
    cost.drawItems      = static_cast<uint32_t>(snap.items.size());
    cost.paintedWidgets = perf.paintedWidgets;
    cost.rebuiltWidgets = perf.rebuiltWidgets;
    cost.layoutMS       = perf.layoutMS;
    cost.paintMS        = perf.paintMS;
    cost.compose        = measureUIFrameComposeReplay(snap);
    if (layout != nullptr) {
        cost.arrangeCount = layout->getArrangeCount();
    }
    return cost;
}

void reportScene(const char* name, const FSceneCost& cost)
{
    std::cout << "[GAH-004] " << name
              << " drawItems=" << cost.drawItems
              << " clipped=" << cost.compose.clippedItemCount
              << " flush=" << cost.compose.screenFlushCount
              << " scissor=" << cost.compose.scissorTransitionCount
              << " painted=" << cost.paintedWidgets
              << " rebuilt=" << cost.rebuiltWidgets
              << " arrange=" << cost.arrangeCount
              << " layoutMS=" << cost.layoutMS
              << " paintMS=" << cost.paintMS
              << '\n';
}

} // namespace

TEST(SceneCostBaselineTest, ClippedLongListFlushTracksClippedItems)
{
    registerBaselineFont();

    WidgetTree tree({.width = 280, .height = 220});
    auto       scroll = std::make_shared<UIScrollViewport>("ClippedList");
    auto       column = std::make_shared<UIContainer>("Rows");
    column->setDirection(EWidgetBoxLayout::Vertical);
    column->setSpacing(2.0f);

    FCanvasSlotArgs scrollSlot;
    scrollSlot.fixedSize = {260.0f, 200.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), scroll, scrollSlot).valid());
    tree.attach(*scroll, column);

    constexpr int kRows = 80;
    for (int i = 0; i < kRows; ++i) {
        auto row = std::make_shared<UIText>("Row" + std::to_string(i));
        row->setText("row_" + std::to_string(i));
        row->_fontSize = 16;
        tree.attach(*column, row);
    }

    UILayout* layout = &scroll->getScrollLayout();
    const FSceneCost cost = measureScene(tree, layout);
    reportScene("clipped_list", cost);

    EXPECT_GE(cost.drawItems, static_cast<uint32_t>(kRows));
    EXPECT_GE(cost.compose.clippedItemCount, static_cast<uint32_t>(kRows));
    EXPECT_EQ(cost.compose.screenFlushCount, cost.compose.clippedItemCount);
    EXPECT_GE(cost.arrangeCount, 1u);
    EXPECT_GE(cost.rebuiltWidgets, 1u);
}

TEST(SceneCostBaselineTest, InspectorColumnRecordsDrawAndArrange)
{
    registerBaselineFont();

    WidgetTree tree({.width = 320, .height = 720});
    auto       column = std::make_shared<UIContainer>("Inspector");
    column->setDirection(EWidgetBoxLayout::Vertical);
    column->setSpacing(2.0f);

    FCanvasSlotArgs columnSlot;
    columnSlot.fixedSize = {300.0f, 700.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), column, columnSlot).valid());

    constexpr int kRows = 250;
    for (int i = 0; i < kRows; ++i) {
        auto row = std::make_shared<UIText>("Row" + std::to_string(i));
        row->setText("prop_" + std::to_string(i));
        row->_fontSize = 16;
        tree.attach(*column, row);
    }

    UILayout* layout = &column->getBoxLayout();
    const FSceneCost cost = measureScene(tree, layout);
    reportScene("inspector", cost);

    EXPECT_GE(cost.drawItems, static_cast<uint32_t>(kRows));
    EXPECT_EQ(cost.compose.itemCount, cost.drawItems);
    EXPECT_GE(cost.arrangeCount, 1u);
    EXPECT_GE(cost.rebuiltWidgets, 1u);
}

TEST(SceneCostBaselineTest, DockWorkspaceRecordsDrawAndArrange)
{
    registerBaselineFont();

    WidgetTree tree({.width = 1280, .height = 800});
    auto       context = std::make_shared<FDockContext>();
    auto       viewport = std::make_shared<UIPanel>("ViewportBody");
    auto       hierarchy = std::make_shared<UIPanel>("HierarchyBody");
    auto       inspector = std::make_shared<UIPanel>("InspectorBody");
    const DockPanelId viewportId =
        context->addPanel("viewport", "Viewport", viewport);
    const DockPanelId hierarchyId =
        context->addPanel("hierarchy", "Hierarchy", hierarchy);
    const DockPanelId inspectorId =
        context->addPanel("inspector", "Inspector", inspector);
    ASSERT_NE(viewportId, kInvalidDockPanelId);
    ASSERT_NE(hierarchyId, kInvalidDockPanelId);
    ASSERT_NE(inspectorId, kInvalidDockPanelId);
    ASSERT_TRUE(context->dockModel().splitLeaf(context->dockModel().getRootNode()->id,
                                               EDockCardinalSide::West,
                                               hierarchyId,
                                               0.22f));
    const FDockNode* viewportLeaf = context->dockModel().findLeafForPanel(viewportId);
    ASSERT_NE(viewportLeaf, nullptr);
    ASSERT_TRUE(context->dockModel().splitLeaf(viewportLeaf->id,
                                               EDockCardinalSide::East,
                                               inspectorId,
                                               0.24f));

    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setContext(context);
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, fillCanvas()).valid());

    UILayout* layout = dock->getLayout();
    ASSERT_NE(layout, nullptr);
    const FSceneCost cost = measureScene(tree, layout);
    reportScene("dock_workspace", cost);

    EXPECT_GE(cost.drawItems, 1u);
    EXPECT_EQ(cost.compose.itemCount, cost.drawItems);
    EXPECT_GE(cost.arrangeCount, 1u);
    EXPECT_GE(cost.compose.screenFlushCount, 1u);
}

TEST(SceneCostBaselineTest, ColorEditPickerRecordsDrawAndFlush)
{
    registerBaselineFont();

    WidgetTree tree({.width = 400, .height = 400});
    auto       edit = std::make_shared<UIColorEdit>("Tint");
    edit->setColor({1.0f, 0.0f, 0.0f, 1.0f});
    FCanvasSlotArgs slot;
    slot.fixedSize = {180.0f, 28.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), edit, slot).valid());
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(12.0f, 14.0f)),
              EWidgetRouteResult::HandledExclusive);
    tree.layout();
    ASSERT_TRUE(edit->isPickerOpen());

    UILayout* layout = edit->getLayout();
    const FSceneCost cost = measureScene(tree, layout);
    reportScene("coloredit_picker", cost);

    EXPECT_GE(cost.drawItems, 8u);
    EXPECT_EQ(cost.compose.itemCount, cost.drawItems);
    EXPECT_GE(cost.compose.screenFlushCount, 1u);
    EXPECT_GE(cost.rebuiltWidgets, 1u);
}

} // namespace ya
