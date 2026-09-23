// Phase 9B editor-scale performance baseline. Guards the retained editor
// hot paths at hierarchy / content-catalog / inspector sizes without a GPU:
//   - UITreeView paints a viewport window, not every expanded row
//   - computeKeyedVisibleWindow stays O(viewport), not O(catalog)
//   - a dense inspector column's second clean snapshot does not rebuild paint
//
// The target links ONLY the GUI closure (same as ToolControlsTest).

#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace ya
{

namespace
{

std::shared_ptr<Font> registerBaselineFont(float fontSize = 16.0f, float advance = 8.0f)
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = fontSize;
    font->lineHeight = fontSize * 1.25f;
    font->ascent     = fontSize;
    font->descent    = fontSize * 0.25f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.size     = {static_cast<int>(advance), static_cast<int>(fontSize)};
        ch.bearing  = {0, 0};
        ch.advance  = {advance, 0.0f};
        ch.bInAtlas = true;
        font->characters[cp] = ch;
    }
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, static_cast<uint32_t>(fontSize), font);
    return font;
}

} // namespace

TEST(EditorScaleBaselineTest, LargeHierarchyPaintStaysViewportBounded)
{
    registerBaselineFont();

    WidgetTree tree({.width = 280, .height = 240});
    auto       viewport = std::make_shared<UIScrollViewport>("HierarchyScroll");
    auto       view     = std::make_shared<UITreeView>("Hierarchy");
    view->_rowHeight    = 20.0f;

    constexpr int kParents  = 50;
    constexpr int kChildren = 40;
    auto          roots     = std::make_shared<ReactiveList<UITreeView::FNode>>();
    for (int p = 0; p < kParents; ++p) {
        UITreeView::FNode parent;
        parent.id    = "p" + std::to_string(p);
        parent.label = "Parent " + std::to_string(p);
        for (int c = 0; c < kChildren; ++c) {
            parent.children.push_back(UITreeView::FNode{
                .id    = parent.id + ".c" + std::to_string(c),
                .label = "Child " + std::to_string(c),
            });
        }
        roots->push(std::move(parent));
    }
    view->bindData(roots);
    for (int p = 0; p < kParents; ++p) {
        view->setExpanded("p" + std::to_string(p), true);
    }

    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {280.0f, 220.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    tree.attach(*viewport, view);
    tree.layout();
    tree.buildSnapshot(UIFrameBuildContext{});
    const GuiPerfStats first = tree.getPerfStats();

    constexpr int kVisibleRows = kParents + kParents * kChildren;
    EXPECT_EQ(view->getVisibleRowCount(), kVisibleRows);
    EXPECT_LT(view->getPaintedRowCount(), 30u);
    EXPECT_GE(view->getPaintedRowCount(), 10u);
    EXPECT_LT(view->getPaintedRowCount(), static_cast<size_t>(kVisibleRows));

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(tree.getPerfStats().drawItems, first.drawItems);
}

TEST(EditorScaleBaselineTest, LargeInspectorColumnCleanSnapshotDoesNotRebuild)
{
    registerBaselineFont();

    WidgetTree tree({.width = 320, .height = 720});
    auto       column = std::make_shared<UIContainer>("Inspector");
    column->setDirection(EWidgetBoxLayout::Vertical);
    column->setSpacing(2.0f);

    FCanvasSlotArgs columnSlot;
    columnSlot.fixedSize = {300.0f, 700.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), column, columnSlot);

    constexpr int kRows = 250;
    for (int i = 0; i < kRows; ++i) {
        auto row = std::make_shared<UIText>("Row" + std::to_string(i));
        row->setText("prop_" + std::to_string(i));
        row->_fontSize = 16;
        tree.attach(*column, row);
    }

    const UIFrameSnapshot firstSnap = tree.buildSnapshot(UIFrameBuildContext{});
    const GuiPerfStats    first     = tree.getPerfStats();
    EXPECT_GE(first.rebuiltWidgets, 1u);
    EXPECT_GE(firstSnap.items.size(), static_cast<size_t>(kRows));

    const UIFrameSnapshot secondSnap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(tree.getPerfStats().drawItems, first.drawItems);
    EXPECT_EQ(secondSnap.items.size(), firstSnap.items.size());
}

TEST(EditorScaleBaselineTest, ContentCatalogWindowIndependentOfItemCount)
{
    constexpr float kItemExtent     = 22.0f;
    constexpr float kItemSpacing    = 2.0f;
    constexpr float kViewportExtent = 200.0f;
    constexpr size_t kOverscan      = 2;

    const FKeyedVisibleWindow small =
        computeKeyedVisibleWindow(50, kItemExtent, kItemSpacing, kViewportExtent, 0.0f, kOverscan);
    const FKeyedVisibleWindow large =
        computeKeyedVisibleWindow(5000, kItemExtent, kItemSpacing, kViewportExtent, 0.0f, kOverscan);
    const FKeyedVisibleWindow scrolled =
        computeKeyedVisibleWindow(5000, kItemExtent, kItemSpacing, kViewportExtent, 2400.0f, kOverscan);

    EXPECT_EQ(small.count, large.count);
    EXPECT_LT(large.count, 20u);
    EXPECT_LT(scrolled.count, 20u);
    EXPECT_GT(large.count, 0u);
    EXPECT_GT(scrolled.count, 0u);
    EXPECT_EQ(large.first, 0u);
    EXPECT_GT(scrolled.first, 0u);
    EXPECT_FLOAT_EQ(large.contentExtent, 5000.0f * kItemExtent + 4999.0f * kItemSpacing);
    EXPECT_GT(large.trailingExtent, 0.0f);
}

} // namespace ya
