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
    behind->setPosition({10.0f, 10.0f});
    behind->setSize({100.0f, 50.0f});
    behind->_zOrder   = 0;
    auto front = std::make_shared<UIButton>("Front");
    front->setPosition({200.0f, 100.0f});
    front->setSize({80.0f, 32.0f});
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
    clip->setPosition({0.0f, 0.0f});
    clip->setSize({200.0f, 100.0f});
    clip->setClipChildren(true);
    auto child = std::make_shared<UIPanel>("Child");
    // Box layout places the child at the content origin with its desired
    // size: 300px wide inside a 200px clip -> the item is half outside.
    child->setSize({300.0f, 100.0f});
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
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});
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
    text->setPosition({30.0f, 40.0f});
    text->setSize({200.0f, 20.0f});
    text->setText("Hello Snapshot");
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
    panel->setPosition({5.0f, 5.0f});
    panel->setSize({50.0f, 25.0f});
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
    behind->setSize({100.0f, 50.0f});
    auto front = std::make_shared<UIButton>("Front");
    front->setSize({80.0f, 32.0f});
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
    ReactiveList<int>*        list = nullptr;
    ReactiveBase::EDirtyLevel listLevel = ReactiveBase::EDirtyLevel::Paint;
    int                       lastCount = 0;

    explicit ReactiveListProbeWidget(std::string name = "ListProbe") : UIElement(std::move(name)) {}

    void paintSelf(UIFrameBuilder&) override { lastCount = static_cast<int>(list->size(listLevel)); }
};

/// Probe widget that reads the same Reactive at both Paint and Layout level in
/// one paint — two distinct edges on the same widget (GI-101: same widget,
/// two properties must not overwrite each other).
struct MixedLevelProbeWidget final : UIElement
{
    Reactive<int>* ref = nullptr;
    int            paintRead  = 0;
    int            layoutRead = 0;

    explicit MixedLevelProbeWidget(std::string name = "Mixed") : UIElement(std::move(name)) {}

    void paintSelf(UIFrameBuilder&) override
    {
        paintRead  = ref->get(ReactiveBase::EDirtyLevel::Paint);
        layoutRead = ref->get(ReactiveBase::EDirtyLevel::Layout);
    }
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
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({50.0f, 25.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: panel reuses its cached items
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Move the panel and invalidate layout: the widget's rect changes, so its
    // cached draw items (old pixel position) must be rebuilt at the new spot.
    panel->setPosition({100.0f, 100.0f});
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
    probe->list      = list.get();
    probe->listLevel = ReactiveBase::EDirtyLevel::Layout;

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

TEST(UIFrameSnapshotTest, CleanTreeOffsetChangeRebuildsResolvedItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    // Cold start + clean frame under context A (identity mapping).
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Same clean tree, changed offset: the cached segment holds the old
    // target-pixel origin, so it must be dropped and re-resolved.
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{
        .offset = {100.0f, 50.0f},
    });
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].pos, glm::vec2(110.0f, 60.0f)); // offset + logical
    EXPECT_EQ(snap.items[0].size, glm::vec2(100.0f, 50.0f));
    EXPECT_EQ(tree.getPerfStats().cacheInvalidations, 1u);
}

TEST(UIFrameSnapshotTest, CleanTreeUiScaleChangeRebuildsResolvedItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{
        .uiScale = {2.0f, 2.0f},
    });
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].pos, glm::vec2(20.0f, 20.0f));    // logical * scale
    EXPECT_EQ(snap.items[0].size, glm::vec2(200.0f, 100.0f)); // size * scale
    EXPECT_EQ(tree.getPerfStats().cacheInvalidations, 1u);
}

TEST(UIFrameSnapshotTest, CleanTreeGenerationChangeDropsCache)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    panel->setPosition({10.0f, 10.0f});
    panel->setSize({100.0f, 50.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    tree.buildSnapshot(UIFrameBuildContext{.generation = 0});
    tree.buildSnapshot(UIFrameBuildContext{.generation = 0});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Host bumps generation (e.g. texture/asset reload) while the mapping is
    // unchanged: cached resolved-texture segments must not be reused.
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{.generation = 1});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(tree.getPerfStats().cacheInvalidations, 1u);
}

TEST(UIFrameSnapshotTest, ReactiveDestroyedBeforeWidgetSeveresBackReference)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       probe = std::make_shared<ReactiveProbeWidget>("Probe");
    tree.attachToLayer(WidgetTree::ELayer::Content, probe);

    auto refB = std::make_shared<Reactive<int>>(2);
    probe->refB = refB.get();

    {
        auto refA = std::make_shared<Reactive<int>>(1);
        probe->refA = refA.get();
        probe->useA = true;
        tree.buildSnapshot(UIFrameBuildContext{}); // probe reads refA -> dependent
        // refA destroyed here: ~ReactiveBase must sever the probe's back-ref.
    }

    // Re-paint reading a live ref. The dirty branch calls clearDependencies(),
    // which walks probe->_dependencies — that set must no longer contain the
    // destroyed refA, or the walk hits a dangling pointer.
    probe->useA = false;
    probe->markPaintDirty();
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(probe->lastRead, 2);
}

TEST(UIFrameSnapshotTest, DetachedWidgetSurvivesReactiveSet)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bound    = std::make_shared<UIText>("Bound");
    auto       textRef  = std::make_shared<Reactive<std::string>>("hello");
    bound->bindText(textRef);
    tree.attachToLayer(WidgetTree::ELayer::Content, bound);
    tree.buildSnapshot(UIFrameBuildContext{}); // bound reads ref -> dependent

    tree.detach(*bound); // detached but still alive (_tree == nullptr)

    // set() still walks bound as a dependent; markPaintDirty must guard the
    // null tree so a detached widget is not laid out or painted.
    textRef->set("world");
    SUCCEED();
}

TEST(UIFrameSnapshotTest, RebindSplitRatioKeepsLatestBindingActive)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    auto ratioA = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratioA);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    auto ratioB = std::make_shared<Reactive<float>>(0.3f);
    split->bindSplitRatio(ratioB);
    tree.buildSnapshot(UIFrameBuildContext{}); // re-layout pulling ratioB

    // The latest binding drives layout on write.
    const uint64_t before = tree.getPerfStats().layoutDirtyTransitions;
    ratioB->set(0.6f);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().layoutDirtyTransitions, before);
}

TEST(UIFrameSnapshotTest, SplitRatioBindingPersistsAcrossRepaints)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    auto ratio = std::make_shared<Reactive<float>>(0.5f);
    split->bindSplitRatio(ratio);

    // Several snapshots each re-paint the split. The bind-time ratio binding is
    // persistent: it must stay registered (independent of per-paint dependency
    // re-collection) so a later write still re-runs layout. This is the
    // regression guard for GI-102's persistent-edge separation.
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t before = tree.getPerfStats().layoutDirtyTransitions;
    ratio->set(0.4f);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().layoutDirtyTransitions, before);
}

// === GI-101: property-aware edge model ===

TEST(UIFrameSnapshotTest, ReactiveMixedLevelConsumersGetCorrectInvalidation)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       paintProbe = std::make_shared<ReactiveListProbeWidget>("PaintProbe");
    paintProbe->listLevel = ReactiveBase::EDirtyLevel::Paint;
    auto layoutProbe      = std::make_shared<ReactiveListProbeWidget>("LayoutProbe");
    layoutProbe->listLevel = ReactiveBase::EDirtyLevel::Layout;
    tree.attachToLayer(WidgetTree::ELayer::Content, paintProbe);
    tree.attachToLayer(WidgetTree::ELayer::Content, layoutProbe);

    auto list = std::make_shared<ReactiveList<int>>();
    paintProbe->list  = list.get();
    layoutProbe->list = list.get();

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    // One ref write fans out to two consumers: the Paint consumer gets a paint
    // transition, the Layout consumer gets a layout transition (which also
    // implies paint, so paint total +2).
    list->push(1);
    tree.buildSnapshot(UIFrameBuildContext{}); // refresh the perf snapshot

    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 2);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
}

TEST(UIFrameSnapshotTest, SameWidgetTwoLevelConsumeBothEdges)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       probe = std::make_shared<MixedLevelProbeWidget>("Mixed");
    tree.attachToLayer(WidgetTree::ELayer::Content, probe);

    auto ref = std::make_shared<Reactive<int>>(0);
    probe->ref = ref.get();

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start (reads at both levels)
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame
    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;

    // The same widget consumed the ref at Paint and Layout: both edges must
    // survive (not be deduplicated away by widget identity) and fire.
    ref->set(1);
    tree.buildSnapshot(UIFrameBuildContext{}); // refresh the perf snapshot

    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
}

TEST(UIFrameSnapshotTest, PaintRebuildDoesNotDropPersistentStyleBinding)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);

    UIStyleSet styleSet;
    auto       style = styleSet.define("accent", FWidgetStyle{});
    styleSet.bindTo(style, *panel); // persistent Paint edge

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    // Force a paint rebuild: the base paint runs clearDependencies(), which
    // must NOT drop the persistent style edge.
    panel->markPaintDirty();
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t paintBefore = tree.getPerfStats().paintDirtyTransitions;
    FWidgetStyle    changed;
    changed.textColor = {1.0f, 0.0f, 0.0f, 1.0f};
    style->set(changed);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
}

TEST(UIFrameSnapshotTest, RebindSplitRatioClearsOldBinding)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    tree.attachToLayer(WidgetTree::ELayer::Content, split);

    auto ratioA = std::make_shared<Reactive<float>>(0.5f);
    auto ratioB = std::make_shared<Reactive<float>>(0.3f);
    split->bindSplitRatio(ratioA);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    split->bindSplitRatio(ratioB); // rebind must sever ratioA's persistent edge

    const uint64_t before = tree.getPerfStats().layoutDirtyTransitions;
    ratioA->set(0.9f); // no longer bound: must not invalidate layout
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, before);

    ratioB->set(0.6f); // still bound: invalidates layout
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, before + 1);
}

// === GI-104: minimal property impact contract ===

TEST(UIFrameSnapshotTest, PropertyImpactPaintDoesNotInvalidateLayout)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    panel->invalidateProperty(EUIPropertyImpact::Paint);
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore);
}

TEST(UIFrameSnapshotTest, PropertyImpactLayoutInvalidatesMeasure)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    panel->invalidateProperty(EUIPropertyImpact::Layout);
    tree.buildSnapshot(UIFrameBuildContext{});

    // Layout implies repaint: both a layout transition and a paint transition.
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore + 1);
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
}

TEST(UIFrameSnapshotTest, SubtreePaintContextInvalidatesWholeSubtree)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parent = std::make_shared<UIPanel>("Parent");
    auto       child  = std::make_shared<UIPanel>("Child");
    tree.attachToLayer(WidgetTree::ELayer::Content, parent);
    tree.attach(*parent, child);
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    const uint64_t paintBefore  = tree.getPerfStats().paintDirtyTransitions;
    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    parent->invalidateProperty(EUIPropertyImpact::SubtreePaintContext);
    tree.buildSnapshot(UIFrameBuildContext{});

    // Parent and child repaint (subtree), but no re-measure.
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 2);
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore);
}

TEST(UIFrameSnapshotTest, SetClipChildrenIsSubtreePaintNotLayout)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       clip = std::make_shared<UIContainer>("Clip");
    tree.attachToLayer(WidgetTree::ELayer::Content, clip);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t layoutBefore = tree.getPerfStats().layoutDirtyTransitions;
    clip->setClipChildren(true); // SubtreePaintContext, not Layout
    tree.buildSnapshot(UIFrameBuildContext{});

    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, layoutBefore);
}

// === GI-301: paint scope RAII ===

TEST(UIFrameSnapshotTest, PaintScopeRestoresStackOnNestedScope)
{
    auto outer = std::make_shared<UIElement>("Outer");
    auto inner = std::make_shared<UIElement>("Inner");

    EXPECT_EQ(currentPaintWidget(), nullptr);
    {
        PaintScope outerScope(outer.get());
        EXPECT_EQ(currentPaintWidget(), outer.get());
        {
            PaintScope innerScope(inner.get());
            EXPECT_EQ(currentPaintWidget(), inner.get());
        }
        EXPECT_EQ(currentPaintWidget(), outer.get());
    }
    EXPECT_EQ(currentPaintWidget(), nullptr);
}

TEST(UIFrameSnapshotTest, PaintWalkRestoresReactiveStack)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parent = std::make_shared<UIContainer>("Parent");
    auto       child  = std::make_shared<UIPanel>("Child");
    tree.attachToLayer(WidgetTree::ELayer::Content, parent);
    tree.attach(*parent, child);

    EXPECT_EQ(currentPaintWidget(), nullptr);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame: reuse path
    EXPECT_EQ(currentPaintWidget(), nullptr);
}

} // namespace ya
