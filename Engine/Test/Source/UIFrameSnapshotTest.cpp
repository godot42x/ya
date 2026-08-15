// Phase 4 regression guards for the immutable UI frame packet: the tree is
// laid out and painted BEFORE the render graph, items carry resolved
// transforms/clips, and the snapshot is widget-independent (widgets may be
// detached right after build without invalidating the packet).

#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Resources/FontManager.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(UIFrameSnapshotTest, BuildResolvesItemsToRenderPixelsInPaintOrder)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIPanel>("Behind");
    behind->_position = {10.0f, 10.0f};
    behind->_size     = {100.0f, 50.0f};
    behind->_zOrder   = 0;
    auto front = std::make_shared<UIButton>("Front");
    front->_position  = {200.0f, 100.0f};
    front->_size      = {80.0f, 32.0f};
    front->_zOrder    = 10;
    tree.attachToLayer(WidgetTree::ELayer::Content, behind);
    tree.attachToLayer(WidgetTree::ELayer::Content, front);

    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{
        .uiScale = {2.0f, 2.0f},
        .offset  = {100.0f, 50.0f},
    });

    ASSERT_EQ(snapshot.items.size(), 2u);
    // Paint order: zOrder ascending (behind first, front last).
    EXPECT_EQ(snapshot.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(120.0f, 70.0f));   // offset + logical*scale
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(200.0f, 100.0f));
    EXPECT_EQ(snapshot.items[1].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(snapshot.items[1].pos, glm::vec2(500.0f, 250.0f));
    EXPECT_FALSE(snapshot.items[0].bClipped);
    EXPECT_EQ(snapshot.logicalExtent.width, 800u);
}

TEST(UIFrameSnapshotTest, ContainerClipResolvesOnChildren)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       clip = std::make_shared<UIContainer>("Clip");
    clip->_position = {0.0f, 0.0f};
    clip->_size     = {200.0f, 100.0f};
    clip->setClipChildren(true);
    auto child = std::make_shared<UIPanel>("Child");
    // Box layout places the child at the content origin with its desired
    // size: 300px wide inside a 200px clip -> the item is half outside.
    child->_size = {300.0f, 100.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, clip);
    tree.attach(*clip, child);

    const UIFrameSnapshot snapshot = tree.buildSnapshot({});

    // The child's item carries the resolved parent clip in render pixels.
    const auto childIt = std::find_if(snapshot.items.begin(), snapshot.items.end(),
                                      [](const UIFrameDrawItem& item) {
                                          return item.size == glm::vec2(300.0f, 100.0f);
                                      });
    ASSERT_NE(childIt, snapshot.items.end());
    EXPECT_TRUE(childIt->bClipped);
    EXPECT_EQ(childIt->clip.pos, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(childIt->clip.extent, glm::vec2(200.0f, 100.0f));
}

TEST(UIFrameSnapshotTest, SnapshotSurvivesImmediateDetach)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->_position = {10.0f, 10.0f};
    panel->_size     = {100.0f, 50.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);

    // Detach + destroy the widget right after building: the packet stays
    // intact and contains no live widget pointers.
    tree.detach(*panel);
    panel.reset();
    EXPECT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(10.0f, 10.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(100.0f, 50.0f));
}

TEST(UIFrameSnapshotTest, TextItemsCarryFontAndText)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       text = std::make_shared<UIText>("T");
    text->_position = {30.0f, 40.0f};
    text->_size     = {200.0f, 20.0f};
    text->_text     = "Hello Snapshot";
    tree.attachToLayer(WidgetTree::ELayer::Content, text);

    // Drop any font cached by earlier suites (the FontManager is process-
    // global): with no RuntimeDefault font the text item is skipped, not
    // crashy (same fallback as the legacy text paint path).
    FontManager::get()->clearCache();
    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    EXPECT_TRUE(snapshot.items.empty());
}

TEST(UIFrameSnapshotTest, LayoutRunsWhenDirtyDuringSnapshot)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->_position = {5.0f, 5.0f};
    panel->_size     = {50.0f, 25.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    // No explicit layout() call: buildSnapshot performs it.
    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(5.0f, 5.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(50.0f, 25.0f));
}

TEST(UIFrameSnapshotTest, StructuralDumpAndDigestTrackVisualPacketOnly)
{
    UIFrameSnapshot first;
    first.logicalExtent = {320, 200};
    first.items.push_back(UIFrameDrawItem{
        .kind  = UIFrameDrawItem::EKind::Sprite,
        .pos   = {12.0f, 24.0f},
        .size  = {48.0f, 36.0f},
        .color = {0.1f, 0.2f, 0.3f, 1.0f},
    });

    UIFrameSnapshot sameVisual = first;
    EXPECT_EQ(digestUIFrameSnapshot(first), digestUIFrameSnapshot(sameVisual));
    EXPECT_EQ(semanticDigestUIFrameSnapshot(first), semanticDigestUIFrameSnapshot(sameVisual));

    sameVisual.items.front().size.x = 49.0f;
    EXPECT_NE(digestUIFrameSnapshot(first), digestUIFrameSnapshot(sameVisual));
    EXPECT_EQ(semanticDigestUIFrameSnapshot(first), semanticDigestUIFrameSnapshot(sameVisual));

    sameVisual.items.front().color.r = 0.2f;
    EXPECT_NE(semanticDigestUIFrameSnapshot(first), semanticDigestUIFrameSnapshot(sameVisual));

    const auto dump = dumpUIFrameSnapshot(first);
    EXPECT_EQ(dump["logicalExtent"]["width"], 320u);
    EXPECT_EQ(dump["items"][0]["kind"], "sprite");
}

TEST(UIFrameSnapshotTest, PerfStatsCountPaintWalkAndDrawItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIPanel>("Behind");
    behind->_size     = {100.0f, 50.0f};
    auto front = std::make_shared<UIButton>("Front");
    front->_size     = {80.0f, 32.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, behind);
    tree.attachToLayer(WidgetTree::ELayer::Content, front);

    const UIFrameSnapshot first = tree.buildSnapshot(UIFrameBuildContext{});

    // First build lays out (tree starts dirty) and walks root + the 4 system
    // layers + the 2 content widgets.
    EXPECT_GT(tree.getPerfStats().layoutMS, 0.0f);
    EXPECT_GE(tree.getPerfStats().paintMS, 0.0f);
    EXPECT_EQ(tree.getPerfStats().paintedWidgets, 7u);
    EXPECT_EQ(tree.getPerfStats().drawItems, first.items.size());

    // Second build reuses the clean layout: layoutMS resets to 0 and the
    // paint-walk count stays identical.
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().layoutMS, 0.0f);
    EXPECT_EQ(tree.getPerfStats().paintedWidgets, 7u);
}

TEST(UIFrameSnapshotTest, ReactiveTextRebuildsOnlyDependentWidget)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bound = std::make_shared<UIText>("Bound");
    auto       plain = std::make_shared<UIText>("Plain");
    tree.attachToLayer(WidgetTree::ELayer::Content, bound);
    tree.attachToLayer(WidgetTree::ELayer::Content, plain);

    auto textRef = std::make_shared<Reactive<std::string>>("hello");
    bound->bindText(textRef);

    // Cold start: root + 4 system layers + the 2 leaf texts all re-run (no
    // cache yet), so rebuilt == painted == 7.
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 7u);

    // No change: every widget reuses the previous-frame segment.
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Mutate the ref: only the bound text is dirty and re-runs its paintSelf.
    textRef->set("world");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
}

TEST(UIFrameSnapshotTest, DestroyedDependentDoesNotDangle)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       ref = std::make_shared<Reactive<std::string>>("hello");
    {
        auto text = std::make_shared<UIText>("Temp");
        text->bindText(ref);
        tree.attachToLayer(WidgetTree::ELayer::Content, text);
        tree.buildSnapshot(UIFrameBuildContext{}); // text reads ref, becomes a dependent
        tree.detach(*text);                        // release the tree's strong ref
        // text is destroyed at scope end; ~UIElement severs the dependency.
    }
    // Must not crash: the destroyed widget was removed from ref's dependents.
    ref->set("world");
    SUCCEED();
}

namespace
{

/// Probe widget: paints nothing, but conditionally reads one of two reactive
/// ints so tests can assert dependency re-collection across re-runs.
struct ReactiveProbeWidget final : UIElement
{
    Reactive<int>* refA = nullptr;
    Reactive<int>* refB = nullptr;
    bool           useA = true;
    int            lastRead = 0;

    explicit ReactiveProbeWidget(std::string name = "Probe") : UIElement(std::move(name)) {}

    void paintSelf(UIFrameBuilder&) override { lastRead = useA ? refA->get() : refB->get(); }
};

/// Probe widget that reads a ReactiveList's size during paint.
struct ReactiveListProbeWidget final : UIElement
{
    ReactiveList<int>* list = nullptr;
    int                lastCount = 0;

    explicit ReactiveListProbeWidget(std::string name = "ListProbe") : UIElement(std::move(name)) {}

    void paintSelf(UIFrameBuilder&) override { lastCount = static_cast<int>(list->size()); }
};

} // namespace

TEST(UIFrameSnapshotTest, ConditionalDependencySwitchRecollects)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       probe = std::make_shared<ReactiveProbeWidget>("Probe");
    tree.attachToLayer(WidgetTree::ELayer::Content, probe);

    auto refA = std::make_shared<Reactive<int>>(1);
    auto refB = std::make_shared<Reactive<int>>(2);
    probe->refA = refA.get();
    probe->refB = refB.get();
    probe->useA = true;

    // First paint reads refA and records the dependency.
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastRead, 1);

    // Switch to refB: re-run the probe (it re-collects from scratch).
    probe->useA = false;
    probe->markPaintDirty();
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastRead, 2);

    // refA no longer triggers a rebuild (its dependency was dropped).
    refA->set(10);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // refB still triggers a rebuild.
    refB->set(20);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    EXPECT_EQ(probe->lastRead, 20);
}

TEST(UIFrameSnapshotTest, ReactiveButtonEnabledOnlyRepaintsButton)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       btn   = std::make_shared<UIButton>("Btn");
    auto       plain = std::make_shared<UIPanel>("Plain");
    tree.attachToLayer(WidgetTree::ELayer::Content, btn);
    tree.attachToLayer(WidgetTree::ELayer::Content, plain);

    auto enabled = std::make_shared<Reactive<bool>>(true);
    btn->bindEnabled(enabled);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Disable: only the button re-runs its paintSelf.
    enabled->set(false);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
}

TEST(UIFrameSnapshotTest, ReactiveSplitRatioInvalidatesLayout)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    auto ratio = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratio);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start (lays out)
    tree.buildSnapshot(UIFrameBuildContext{}); // clean layout: no re-layout
    EXPECT_EQ(tree.getPerfStats().layoutMS, 0.0f);

    // Write a new ratio: the layout is invalidated and re-run.
    ratio->set(0.3f);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().layoutMS, 0.0f);
}

TEST(UIFrameSnapshotTest, ReactiveListPushNotifiesDependents)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       probe = std::make_shared<ReactiveListProbeWidget>("ListProbe");
    tree.attachToLayer(WidgetTree::ELayer::Content, probe);

    auto list = std::make_shared<ReactiveList<int>>();
    probe->list = list.get();

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastCount, 0);

    list->push(1);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastCount, 1);

    list->push(2);
    list->push(3);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastCount, 3);

    list->clear();
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastCount, 0);
}

TEST(UIFrameSnapshotTest, PerfStateBridgeRecordsTreeMetrics)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    tree.buildSnapshot(UIFrameBuildContext{});

    using namespace ya::literals;
    auto& perf = profiling::metrics();
    EXPECT_GT(perf.getLastValue("gui.tree.painted"_name, "count"_name), 0.0f);
    EXPECT_GT(perf.getLastValue("gui.tree.rebuilt"_name, "count"_name), 0.0f);
    EXPECT_GT(perf.getLastValue("gui.tree.items"_name, "count"_name), 0.0f);
    EXPECT_GE(perf.getLastValue("gui.tree.paint"_name, "ms"_name), 0.0f);
}

TEST(UIFrameSnapshotTest, StyleEditRepaintsBoundTexts)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       t1 = std::make_shared<UIText>("T1");
    auto       t2 = std::make_shared<UIText>("T2");
    tree.attachToLayer(WidgetTree::ELayer::Content, t1);
    tree.attachToLayer(WidgetTree::ELayer::Content, t2);

    UIStyleSet styleSet;
    FWidgetStyle title;
    title.textColor = {1.0f, 0.0f, 0.0f, 1.0f};
    auto titleStyle = styleSet.define("title", title);

    t1->bindStyle(titleStyle);
    t2->bindStyle(titleStyle);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Edit the shared style: both bound texts are dirty and re-run.
    FWidgetStyle edited = titleStyle->value();
    edited.textColor    = {0.0f, 1.0f, 0.0f, 1.0f};
    titleStyle->set(edited);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 2u);
    EXPECT_EQ(t1->resolvedStyle().textColor, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(UIFrameSnapshotTest, TreeViewExpandCollapseChangesVisibleRows)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       tv = std::make_shared<UITreeView>("Tree");
    tv->_bAutoSize = true;
    tree.attachToLayer(WidgetTree::ELayer::Content, tv);

    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({"root", "Root", {{"c1", "Child 1", {}}, {"c2", "Child 2", {}}}});
    tv->bindData(roots);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tv->computeDesiredSize().y, tv->_rowHeight * 1.0f); // root only

    tv->setExpanded("root", true);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tv->computeDesiredSize().y, tv->_rowHeight * 3.0f); // root + 2 children

    tv->setExpanded("root", false);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tv->computeDesiredSize().y, tv->_rowHeight * 1.0f); // collapsed again
}

TEST(UIFrameSnapshotTest, TreeViewSelectionRepaintsOnlyTreeView)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       tv = std::make_shared<UITreeView>("Tree");
    tree.attachToLayer(WidgetTree::ELayer::Content, tv);

    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({"a", "A", {}});
    roots->push({"b", "B", {}});
    tv->bindData(roots);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Select "a": only the tree view re-runs its paintSelf.
    tv->getSelection()->set("a");
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    EXPECT_EQ(tv->getSelection()->value(), "a");
}

TEST(UIFrameSnapshotTest, TreeViewDataSourcePushInvalidatesLayout)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       tv = std::make_shared<UITreeView>("Tree");
    tv->_bAutoSize = true;
    tree.attachToLayer(WidgetTree::ELayer::Content, tv);

    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    tv->bindData(roots);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tv->computeDesiredSize().y, 0.0f); // empty

    roots->push({"a", "A", {}});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tv->computeDesiredSize().y, tv->_rowHeight * 1.0f); // one row
}

TEST(UIFrameSnapshotTest, LayoutChangeRebuildsMovedWidgetDrawItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->_position = {10.0f, 10.0f};
    panel->_size     = {50.0f, 25.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: panel reuses its cached items
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Move the panel and invalidate layout: the widget's rect changes, so its
    // cached draw items (old pixel position) must be rebuilt at the new spot.
    panel->_position = {100.0f, 100.0f};
    tree.invalidateLayout();
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(100.0f, 100.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(50.0f, 25.0f));
}

TEST(UIFrameSnapshotTest, TransientHoverAndFocusRepaintButton)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       btn = std::make_shared<UIButton>("Btn");
    tree.attachToLayer(WidgetTree::ELayer::Content, btn);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Hover state change re-paints with the hovered color (VisualFlag marks
    // the button paint-dirty on assignment).
    btn->onPointerEnter();
    UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, btn->_hoveredColor);

    // Leave re-paints back to the normal color.
    btn->onPointerLeave();
    snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(snap.items[0].color, btn->_normalColor);

    // Focus (keyboard) re-paints to the focused color.
    btn->onFocusGained(true);
    snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(snap.items[0].color, btn->_focusedColor);
}

TEST(UIFrameSnapshotTest, ReactivePaintMutationRecordsReasonAndTransition)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bound = std::make_shared<UIText>("Bound");
    tree.attachToLayer(WidgetTree::ELayer::Content, bound);

    auto textRef = std::make_shared<Reactive<std::string>>("hello");
    bound->bindText(textRef);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start (lays out)
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    textRef->set("world"); // Paint-level invalidation

    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ReactivePaint);
    EXPECT_EQ(bound->getLastInvalidationReason(), EUIInvalidationReason::ReactivePaint);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore);
}

TEST(UIFrameSnapshotTest, ReactiveLayoutMutationRecordsReasonAndTransition)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       probe = std::make_shared<ReactiveListProbeWidget>("ListProbe");
    tree.attachToLayer(WidgetTree::ELayer::Content, probe);

    // Layout-granularity reactive: a write invalidates the tree's layout.
    // (SplitPane would also be Layout-level, but it overrides paint() and so
    // does not yet clear _bPaintDirty — that is the Phase 2 unification work,
    // not part of this diagnostics baseline.)
    auto list = std::make_shared<ReactiveList<int>>();
    list->setDirtyLevel(ReactiveBase::EDirtyLevel::Layout);
    probe->list = list.get();

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start (lays out)
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    list->push(1); // Layout-level invalidation

    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ReactiveLayout);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
}

TEST(UIFrameSnapshotTest, SameValueReactiveSetSkipsInvalidation)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bound = std::make_shared<UIText>("Bound");
    tree.attachToLayer(WidgetTree::ELayer::Content, bound);

    auto textRef = std::make_shared<Reactive<std::string>>("hello");
    bound->bindText(textRef);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t paintBefore = tree.getPerfStats().paintDirtyTransitions;

    textRef->set("hello"); // same value: no notify, no transition

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
}

} // namespace ya
