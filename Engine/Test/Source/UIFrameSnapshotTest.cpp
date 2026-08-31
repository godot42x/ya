#include "GUITestLayoutHelpers.h"
// Phase 4 regression guards for the immutable UI frame packet: the tree is
// laid out and painted BEFORE the render graph, items carry resolved
// transforms/clips, and the snapshot is widget-independent (widgets may be
// detached right after build without invalidating the packet).

#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "Render/Resources/FontManager.h"

#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace ya
{

TEST(UIFrameSnapshotTest, BuildResolvesItemsToRenderPixelsInPaintOrder)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIPanel>("Behind");
    authorSlotPosition(*behind, {10.0f, 10.0f});
    authorSlotSize(*behind, {100.0f, 50.0f});
    behind->_zOrder   = 0;
    auto front = std::make_shared<UIButton>("Front");
    authorSlotPosition(*front, {200.0f, 100.0f});
    authorSlotSize(*front, {80.0f, 32.0f});
    front->_zOrder    = 10;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), behind);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), front);

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
    // G1: every widget is self-clipped by the paint template.
    EXPECT_TRUE(snapshot.items[0].bClipped);
    EXPECT_EQ(snapshot.items[0].clip.pos, glm::vec2(120.0f, 70.0f));
    EXPECT_EQ(snapshot.items[0].clip.extent, glm::vec2(200.0f, 100.0f));
    EXPECT_EQ(snapshot.logicalExtent.width, 800u);
}

TEST(UIFrameSnapshotTest, ContainerClipResolvesOnChildren)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       clip = std::make_shared<UIContainer>("Clip");
    authorSlotPosition(*clip, {0.0f, 0.0f});
    authorSlotSize(*clip, {200.0f, 100.0f});
    clip->setClipChildren(true);
    auto child = std::make_shared<UIPanel>("Child");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), clip);
    clip->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({300.0f, 100.0f});
        }
    });

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
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

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
    authorSlotPosition(*text, {30.0f, 40.0f});
    authorSlotSize(*text, {200.0f, 20.0f});
    text->setText("Hello Snapshot");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);

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
    authorSlotPosition(*panel, {5.0f, 5.0f});
    authorSlotSize(*panel, {50.0f, 25.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    // No explicit layout() call: buildSnapshot performs it.
    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(5.0f, 5.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(50.0f, 25.0f));
}

TEST(UIFrameSnapshotTest, PanelCornerRadiusScalesIntoDrawItem)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    panel->setColor({1.0f, 0.0f, 0.0f, 1.0f});
    panel->setCornerRadius(8.0f);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{
        .uiScale = {2.0f, 2.0f},
    });

    ASSERT_EQ(snapshot.items.size(), 1u);
    // Rounded rect carries the corner radius scaled into target px.
    EXPECT_EQ(snapshot.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_FLOAT_EQ(snapshot.items[0].cornerRadius, 16.0f);
    // Sharp (default) panel produces no corner radius.
    panel->setCornerRadius(0.0f);
    const UIFrameSnapshot sharp = tree.buildSnapshot(UIFrameBuildContext{.uiScale = {2.0f, 2.0f}});
    ASSERT_EQ(sharp.items.size(), 1u);
    EXPECT_FLOAT_EQ(sharp.items[0].cornerRadius, 0.0f);
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

TEST(UIFrameSnapshotTest, SemanticDigestIgnoresFontDependentTextGeometry)
{
    UIFrameSnapshot windowed;
    UIFrameSnapshot headless = windowed;

    windowed.items.push_back(UIFrameDrawItem{
        .kind  = UIFrameDrawItem::EKind::Text,
        .pos   = {14.0f, 6.5f},
        .size  = {32.0f, 17.0f},
        .color = {0.9f, 0.9f, 0.9f, 1.0f},
        .text  = "FEATURE GALLERY",
    });
    headless.items.push_back(UIFrameDrawItem{
        .kind  = UIFrameDrawItem::EKind::Text,
        .pos   = {14.0f, 6.46875f},
        .size  = {31.5f, 16.8f},
        .color = {0.9f, 0.9f, 0.9f, 1.0f},
        .text  = "FEATURE GALLERY",
    });

    EXPECT_NE(digestUIFrameSnapshot(windowed), digestUIFrameSnapshot(headless));
    EXPECT_EQ(semanticDigestUIFrameSnapshot(windowed), semanticDigestUIFrameSnapshot(headless));
}

TEST(UIFrameSnapshotTest, PerfStatsCountPaintWalkAndDrawItems)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIPanel>("Behind");
    authorSlotSize(*behind, {100.0f, 50.0f});
    auto front = std::make_shared<UIButton>("Front");
    authorSlotSize(*front, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), behind);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), front);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bound);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), plain);

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
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), btn);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), plain);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    tree.buildSnapshot(UIFrameBuildContext{});

    using namespace ya::literals;
    auto& perf = profiling::metrics();
    EXPECT_GT(perf.getLastValue("gui.tree.painted"_name, "count"_name), 0.0f);
    EXPECT_GT(perf.getLastValue("gui.tree.rebuilt"_name, "count"_name), 0.0f);
    EXPECT_GT(perf.getLastValue("gui.tree.items"_name, "count"_name), 0.0f);
    EXPECT_GE(perf.getLastValue("gui.tree.paint"_name, "ms"_name), 0.0f);
}

TEST(UIFrameSnapshotTest, StyleEditRepaintsThemedTexts)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       t1 = std::make_shared<UIText>("T1");
    auto       t2 = std::make_shared<UIText>("T2");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), t1);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), t2);

    // Unified binding path (Phase 3 cleanup): un-authored texts resolve the
    // "text" style from the tree theme; editing THAT style marks every
    // dependent paint-dirty through the style's Reactive edge (the legacy
    // FWidgetStyle bindStyle path is gone).
    auto theme       = std::make_shared<UITheme>();
    auto title       = FTextStyle{};
    title.textColor  = {1.0f, 0.0f, 0.0f, 1.0f};
    auto titleStyle  = theme->define<FTextStyle>("text", title);
    tree.setTheme(theme.get());

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Edit the shared style: both themed texts are dirty and re-run.
    FTextStyle edited = titleStyle->value();
    edited.textColor  = {0.0f, 1.0f, 0.0f, 1.0f};
    titleStyle->set(edited);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 2u);
    EXPECT_EQ(t1->resolvedStyle().textColor, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(UIFrameSnapshotTest, PanelResolvesThemeStyleAndRepaintsOnThemeSwitch)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    // Two tree-level themes define the same "panel" key differently
    // (style-system Phase 3): the panel resolves FPanelStyle through the tree
    // theme and must repaint when the theme is swapped (generation token).
    auto dark  = std::make_shared<UITheme>();
    auto darkStyle = FPanelStyle{};
    darkStyle.fillColor = FBrush::solid({0.16f, 0.18f, 0.22f, 1.0f});
    dark->define<FPanelStyle>("panel", darkStyle);
    auto light = std::make_shared<UITheme>();
    auto lightStyle = FPanelStyle{};
    lightStyle.fillColor = FBrush::solid({0.94f, 0.95f, 0.97f, 1.0f});
    light->define<FPanelStyle>("panel", lightStyle);

    tree.setTheme(dark.get());
    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    const auto findPanelSprite = [&](const UIFrameSnapshot& s) -> const UIFrameDrawItem*
    {
        for (const UIFrameDrawItem& item : s.items) {
            if (item.kind == UIFrameDrawItem::EKind::Sprite && item.size == glm::vec2(100.0f, 50.0f)) {
                return &item;
            }
        }
        return nullptr;
    };

    // Dark: the panel paints the themed dark fill.
    const UIFrameSnapshot darkSnap = tree.buildSnapshot(UIFrameBuildContext{});
    const UIFrameDrawItem* darkItem = findPanelSprite(darkSnap);
    ASSERT_NE(darkItem, nullptr);
    EXPECT_EQ(darkItem->color, darkStyle.fillColor.tintColor);

    // Swap the tree theme: the generation token must repaint the panel (no
    // style Reactive value changed, so only the theme-switch edge can mark it
    // dirty).
    tree.setTheme(light.get());
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    const UIFrameSnapshot lightSnap = tree.buildSnapshot(UIFrameBuildContext{});
    const UIFrameDrawItem* lightItem = findPanelSprite(lightSnap);
    ASSERT_NE(lightItem, nullptr);
    EXPECT_EQ(lightItem->color, lightStyle.fillColor.tintColor);
}

TEST(UIFrameSnapshotTest, TreeViewExpandCollapseChangesVisibleRows)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       tv = std::make_shared<UITreeView>("Tree");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tv);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tv);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tv);

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
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {50.0f, 25.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: panel reuses its cached items
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Move the panel and invalidate layout: the widget's rect changes, so its
    // cached draw items (old pixel position) must be rebuilt at the new spot.
    authorSlotPosition(*panel, {100.0f, 100.0f});
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
    FCanvasSlotArgs buttonArgs;
    buttonArgs.fixedSize = {100.0f, 50.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), btn, buttonArgs).valid());

    // No theme mounted: the button paints the default-constructed
    // FButtonStyle (framework fallback). Phase 3 cleanup removed the bare
    // color fields, so the test compares against the typed style defaults.
    const FButtonStyle fallback;

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: all reuse
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Hover state change re-paints with the hovered color (VisualFlag marks
    // the button paint-dirty on assignment).
    btn->onPointerEnter();
    UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, fallback.hoveredFill.tintColor);

    // Leave re-paints back to the normal color.
    btn->onPointerLeave();
    snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(snap.items[0].color, fallback.normalFill.tintColor);

    // Focus (keyboard) re-paints to the focused color.
    btn->onFocusGained(true);
    snap = tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(snap.items[0].color, fallback.focusedFill.tintColor);
}

TEST(UIFrameSnapshotTest, ReactivePaintMutationRecordsReasonAndTransition)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       bound = std::make_shared<UIText>("Bound");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bound);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bound);

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
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

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
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

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
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bound);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), paintProbe);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), layoutProbe);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), probe);

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

TEST(UIFrameSnapshotTest, PaintRebuildReCollectsStyleEdgeAfterForcedRebuild)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    // Un-authored panel resolves the "panel" style from the tree theme
    // (unified binding path; the old persistent FWidgetStyle bindTo edge is
    // gone — paint-time get() re-collects the dependency every rebuild).
    auto theme = std::make_shared<UITheme>();
    auto style = theme->define<FPanelStyle>("panel", FPanelStyle{});
    tree.setTheme(theme.get());

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame

    // Force a paint rebuild: the base paint runs clearDependencies(), then
    // paintSelf re-reads the themed style and re-collects the dependency, so
    // a later style edit still repaints the panel.
    panel->markPaintDirty();
    tree.buildSnapshot(UIFrameBuildContext{});

    const uint64_t paintBefore = tree.getPerfStats().paintDirtyTransitions;
    FPanelStyle    changed;
    changed.fillColor = FBrush::solid({1.0f, 0.0f, 0.0f, 1.0f});
    style->set(changed);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, paintBefore + 1);
}

TEST(UIFrameSnapshotTest, RebindSplitRatioClearsOldBinding)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);

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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), clip);
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
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent);
    tree.attach(*parent, child);

    EXPECT_EQ(currentPaintWidget(), nullptr);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean frame: reuse path
    EXPECT_EQ(currentPaintWidget(), nullptr);
}

// === GI-302: unified paint template (paintChildren customization) ===

TEST(UIFrameSnapshotTest, ScrollViewportClipsContentToViewportRect)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       viewport = std::make_shared<UIScrollViewport>("Scroll");
    authorSlotSize(*viewport, {200.0f, 60.0f});
    viewport->_bShowScrollbar = false;
    auto content = std::make_shared<UIPanel>("Content");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport);
    viewport->addDetachedChild(content, [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UIOverlaySlot*>(&slot)) {
            single->setPreferredSize({200.0f, 100.0f});
        }
    });

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});

    // The content sprite keeps its full 100px extent but is clipped to the
    // 60px viewport rect (the viewport itself paints no self item).
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_TRUE(snap.items[0].bClipped);
    EXPECT_EQ(snap.items[0].size, glm::vec2(200.0f, 100.0f));
    EXPECT_EQ(snap.items[0].clip.extent, glm::vec2(200.0f, 60.0f));
}

TEST(UIFrameSnapshotTest, SplitPaneClipsChildrenToOwnPaneRect)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       split = std::make_shared<UISplitPane>("Split");
    authorSlotSize(*split, {400.0f, 200.0f});
    split->setSplitRatio(0.5f);
    auto paneA = std::make_shared<UIPanel>("A");
    auto paneB = std::make_shared<UIPanel>("B");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);
    tree.attach(*split, paneA);
    tree.attach(*split, paneB);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});

    // The two pane children are each clipped to their own pane rect (not the
    // full split rect). Collect the clipped items: exactly the two panes.
    std::vector<const UIFrameDrawItem*> clipped;
    for (const auto& item : snap.items) {
        if (item.bClipped) {
            clipped.push_back(&item);
        }
    }
    ASSERT_EQ(clipped.size(), 3u);
    std::set<glm::vec2, bool(*)(const glm::vec2&, const glm::vec2&)> uniquePositions(
        [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    for (const auto* item : clipped) {
        uniquePositions.insert(item->clip.pos);
    }
    EXPECT_GE(uniquePositions.size(), 2u);
}

TEST(UIFrameSnapshotTest, LayoutHostsReuseSelfSegmentWhenClean)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       container = std::make_shared<UIContainer>("C");
    container->setClipChildren(true);
    auto child = std::make_shared<UIPanel>("A");
    authorSlotSize(*child, {40.0f, 20.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), container);
    tree.attach(*container, child);

    auto split = std::make_shared<UISplitPane>("Split");
    auto paneA = std::make_shared<UIPanel>("PA");
    auto paneB = std::make_shared<UIPanel>("PB");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);
    tree.attach(*split, paneA);
    tree.attach(*split, paneB);

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start: all re-run
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: layout hosts reuse self segments
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
}

// === GI-304: inherited context invalidation ===

TEST(UIFrameSnapshotTest, ContainerClipResizeInvalidatesChildSegments)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       clip = std::make_shared<UIContainer>("Clip");
    clip->setClipChildren(true);
    authorSlotSize(*clip, {200.0f, 100.0f});
    auto child = std::make_shared<UIPanel>("Child");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), clip);
    clip->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* box = dynamic_cast<UIBoxSlot*>(&slot)) {
            box->setPreferredSize({50.0f, 25.0f});
        }
    });

    tree.buildSnapshot(UIFrameBuildContext{}); // cold start
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: child reuses its segment
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Widen the clip container. The child keeps its own 50x25 rect, but its
    // cached segment still holds the old 200-wide clip — the clip host's rect
    // change must invalidate the child's resolved segment (GI-304).
    authorSlotSize(*clip, {300.0f, 100.0f});
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});

    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_TRUE(snap.items[0].bClipped);
    EXPECT_EQ(snap.items[0].clip.extent, glm::vec2(50.0f, 100.0f));
}

TEST(UIFrameSnapshotTest, AuthoredButtonStyleWinsOverThemeAndIgnoresThemeSwitch)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    authorSlotPosition(*button, {10.0f, 10.0f});
    authorSlotSize(*button, {80.0f, 32.0f});
    FButtonStyle authored;
    authored.normalFill = FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f});
    button->setStyle(authored);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);

    auto theme = std::make_shared<UITheme>();
    FButtonStyle themed;
    themed.normalFill = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    theme->define<FButtonStyle>("button", themed);
    tree.setTheme(theme.get());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    auto other = std::make_shared<UITheme>();
    FButtonStyle themed2;
    themed2.normalFill = FBrush::solid({0.0f, 1.0f, 0.0f, 1.0f});
    other->define<FButtonStyle>("button", themed2);
    tree.setTheme(other.get());
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(after.items.empty());
    EXPECT_EQ(after.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
}

TEST(UIFrameSnapshotTest, ThemeAttachAfterUnthemedBuildRepaintsKeyedButton)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    authorSlotPosition(*button, {10.0f, 10.0f});
    authorSlotSize(*button, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);

    const UIFrameSnapshot before = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(before.items.empty());
    EXPECT_EQ(before.items.front().color, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));

    auto         theme = std::make_shared<UITheme>();
    FButtonStyle themed;
    themed.normalFill = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    theme->define<FButtonStyle>("button", themed);
    tree.setTheme(theme.get());

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(after.items.empty());
    EXPECT_EQ(after.items.front().color, glm::vec4(0.1f, 0.2f, 0.9f, 1.0f));
}

TEST(UIFrameSnapshotTest, SetColorWritesAuthoredStyleAndBeatsTheme)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto theme = std::make_shared<UITheme>();
    FPanelStyle panelThemed;
    panelThemed.fillColor = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    theme->define<FPanelStyle>("panel", panelThemed);
    FTextStyle textThemed;
    textThemed.textColor = {0.1f, 0.9f, 0.2f, 1.0f};
    theme->define<FTextStyle>("text", textThemed);
    tree.setTheme(theme.get());

    auto panel = std::make_shared<UIPanel>("P");
    authorSlotPosition(*panel, {10.0f, 10.0f});
    authorSlotSize(*panel, {100.0f, 50.0f});
    panel->setColor({0.9f, 0.2f, 0.1f, 1.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);

    auto text = std::make_shared<UIText>("T");
    text->setColor({0.2f, 0.3f, 0.8f, 1.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);

    EXPECT_TRUE(panel->hasAuthoredStyle());
    EXPECT_TRUE(text->hasAuthoredStyle());
    EXPECT_EQ(text->resolvedStyle().textColor, glm::vec4(0.2f, 0.3f, 0.8f, 1.0f));

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));

    auto other = std::make_shared<UITheme>();
    FPanelStyle panelThemed2;
    panelThemed2.fillColor = FBrush::solid({0.0f, 1.0f, 0.0f, 1.0f});
    other->define<FPanelStyle>("panel", panelThemed2);
    FTextStyle textThemed2;
    textThemed2.textColor = {1.0f, 1.0f, 0.0f, 1.0f};
    other->define<FTextStyle>("text", textThemed2);
    tree.setTheme(other.get());
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(text->resolvedStyle().textColor, glm::vec4(0.2f, 0.3f, 0.8f, 1.0f));
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(after.items.empty());
    EXPECT_EQ(after.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
}

TEST(UIFrameSnapshotTest, SameAuthoredStyleDoesNotDirty)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    authorSlotPosition(*button, {10.0f, 10.0f});
    authorSlotSize(*button, {80.0f, 32.0f});
    FButtonStyle style;
    style.normalFill = FBrush::solid({0.2f, 0.3f, 0.4f, 1.0f});
    button->setStyle(style);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    const GuiPerfStats before = tree.getPerfStats();

    button->setStyle(style);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().layoutDirtyTransitions, before.layoutDirtyTransitions);
    EXPECT_EQ(tree.getPerfStats().paintDirtyTransitions, before.paintDirtyTransitions);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
}

TEST(UIFrameSnapshotTest, SparseStyleFieldInheritsUnpatchedFieldsOnThemeSwitch)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    authorSlotPosition(*button, {10.0f, 10.0f});
    authorSlotSize(*button, {80.0f, 32.0f});
    button->setStyleField("normalFill", FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);

    auto         theme = std::make_shared<UITheme>();
    FButtonStyle themed;
    themed.normalFill  = FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f});
    themed.hoveredFill = FBrush::solid({0.2f, 0.9f, 0.2f, 1.0f});
    theme->define<FButtonStyle>("button", themed);
    tree.setTheme(theme.get());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
    {
        const FButtonStyle resolved = resolveWidgetStyle<FButtonStyle>(*button, button->_authoredStyle);
        EXPECT_EQ(resolved.normalFill, FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
        EXPECT_EQ(resolved.hoveredFill, FBrush::solid({0.2f, 0.9f, 0.2f, 1.0f}));
    }

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    auto         other = std::make_shared<UITheme>();
    FButtonStyle themed2;
    themed2.normalFill  = FBrush::solid({0.0f, 1.0f, 0.0f, 1.0f});
    themed2.hoveredFill = FBrush::solid({0.1f, 0.1f, 0.8f, 1.0f});
    other->define<FButtonStyle>("button", themed2);
    tree.setTheme(other.get());
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);

    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(after.items.empty());
    EXPECT_EQ(after.items.front().color, glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
    const FButtonStyle resolved = resolveWidgetStyle<FButtonStyle>(*button, button->_authoredStyle);
    EXPECT_EQ(resolved.normalFill, FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
    EXPECT_EQ(resolved.hoveredFill, FBrush::solid({0.1f, 0.1f, 0.8f, 1.0f}));
}

TEST(UIFrameSnapshotTest, SetColorOverlaysColorAndInheritsThemeFontSize)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       theme = std::make_shared<UITheme>();
    FTextStyle textThemed;
    textThemed.textColor = {0.1f, 0.9f, 0.2f, 1.0f};
    textThemed.fontSize  = 14;
    theme->define<FTextStyle>("text", textThemed);
    tree.setTheme(theme.get());

    auto text = std::make_shared<UIText>("T");
    text->setColor({0.2f, 0.3f, 0.8f, 1.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(text->resolvedStyle().textColor, glm::vec4(0.2f, 0.3f, 0.8f, 1.0f));
    EXPECT_EQ(text->resolvedStyle().fontSize, 14u);

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    auto       other = std::make_shared<UITheme>();
    FTextStyle textThemed2;
    textThemed2.textColor = {1.0f, 1.0f, 0.0f, 1.0f};
    textThemed2.fontSize  = 24;
    other->define<FTextStyle>("text", textThemed2);
    tree.setTheme(other.get());
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(text->resolvedStyle().textColor, glm::vec4(0.2f, 0.3f, 0.8f, 1.0f));
    EXPECT_EQ(text->resolvedStyle().fontSize, 24u);
}

TEST(UIFrameSnapshotTest, ImagePlaceholderAndModalPopupFollowTheme)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto theme = std::make_shared<UITheme>();
    FImageStyle imageStyle;
    imageStyle.placeholderFill = FBrush::solid({0.1f, 0.2f, 0.3f, 1.0f});
    theme->define<FImageStyle>("image", imageStyle);
    FPopupStyle popupStyle;
    popupStyle.modalFill = FBrush::solid({0.4f, 0.0f, 0.0f, 0.5f});
    theme->define<FPopupStyle>("popup", popupStyle);
    tree.setTheme(theme.get());

    auto image = std::make_shared<UIImage>("Img");
    authorSlotPosition(*image, {10.0f, 10.0f});
    authorSlotSize(*image, {40.0f, 40.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image);

    auto overlay = std::make_shared<UIPopupOverlay>("Modal");
    overlay->_bModal = true;
    FCanvasSlotArgs overlayArgs;
    overlayArgs.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), overlay, overlayArgs).valid());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_GE(snap.items.size(), 2u);
    EXPECT_EQ(snap.items[0].color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_EQ(snap.items.back().color, glm::vec4(0.4f, 0.0f, 0.0f, 0.5f));
    EXPECT_FALSE(image->hasAuthoredStyle());
    EXPECT_FALSE(overlay->hasAuthoredStyle());

    auto other = std::make_shared<UITheme>();
    FImageStyle imageStyle2;
    imageStyle2.placeholderFill = FBrush::solid({0.9f, 0.8f, 0.1f, 1.0f});
    other->define<FImageStyle>("image", imageStyle2);
    FPopupStyle popupStyle2;
    popupStyle2.modalFill = FBrush::solid({0.0f, 0.5f, 0.0f, 0.4f});
    other->define<FPopupStyle>("popup", popupStyle2);
    tree.setTheme(other.get());
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_GE(after.items.size(), 2u);
    EXPECT_EQ(after.items[0].color, glm::vec4(0.9f, 0.8f, 0.1f, 1.0f));
    EXPECT_EQ(after.items.back().color, glm::vec4(0.0f, 0.5f, 0.0f, 0.4f));
}

TEST(UIFrameSnapshotTest, TreeViewSelectionFollowsTheme)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto theme = std::make_shared<UITheme>();
    FTreeViewStyle treeStyle;
    treeStyle.selectedFill = FBrush::solid({0.1f, 0.2f, 0.3f, 1.0f});
    theme->define<FTreeViewStyle>("tree", treeStyle);
    tree.setTheme(theme.get());

    auto tv = std::make_shared<UITreeView>("Tree");
    authorSlotSize(*tv, {200.0f, 80.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tv);
    auto roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    roots->push({"a", "A", {}});
    tv->bindData(roots);
    tv->getSelection()->set("a");

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items[0].color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_FALSE(tv->hasAuthoredStyle());

    auto other = std::make_shared<UITheme>();
    FTreeViewStyle treeStyle2;
    treeStyle2.selectedFill = FBrush::solid({0.9f, 0.1f, 0.2f, 1.0f});
    other->define<FTreeViewStyle>("tree", treeStyle2);
    tree.setTheme(other.get());
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(after.items.empty());
    EXPECT_EQ(after.items[0].color, glm::vec4(0.9f, 0.1f, 0.2f, 1.0f));
}

TEST(UIFrameSnapshotTest, DragDropTilesFollowTheme)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto theme = std::make_shared<UITheme>();
    FDragDropStyle sourceStyle;
    sourceStyle.normalFill = FBrush::solid({0.1f, 0.2f, 0.3f, 1.0f});
    theme->define<FDragDropStyle>("drag.source", sourceStyle);
    FDragDropStyle targetStyle;
    targetStyle.normalFill = FBrush::solid({0.4f, 0.0f, 0.0f, 1.0f});
    theme->define<FDragDropStyle>("drag.target", targetStyle);
    tree.setTheme(theme.get());

    auto source = std::make_shared<UIDragDropTile>("Src", UIDragDropTile::EKind::Source);
    authorSlotPosition(*source, {10.0f, 10.0f});
    authorSlotSize(*source, {80.0f, 24.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source);
    auto target = std::make_shared<UIDragDropTile>("Dst", UIDragDropTile::EKind::Target);
    authorSlotPosition(*target, {10.0f, 40.0f});
    authorSlotSize(*target, {80.0f, 24.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target);

    const auto spriteColors = [](const UIFrameSnapshot& snap)
    {
        std::vector<glm::vec4> colors;
        for (const auto& item : snap.items) {
            if (item.kind == UIFrameDrawItem::EKind::Sprite) {
                colors.push_back(item.color);
            }
        }
        return colors;
    };

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    const auto            fills = spriteColors(snap);
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[0], glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_EQ(fills[1], glm::vec4(0.4f, 0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(source->hasAuthoredStyle());
    EXPECT_FALSE(target->hasAuthoredStyle());

    auto other = std::make_shared<UITheme>();
    FDragDropStyle sourceStyle2;
    sourceStyle2.normalFill = FBrush::solid({0.9f, 0.8f, 0.1f, 1.0f});
    other->define<FDragDropStyle>("drag.source", sourceStyle2);
    FDragDropStyle targetStyle2;
    targetStyle2.normalFill = FBrush::solid({0.0f, 0.5f, 0.0f, 1.0f});
    other->define<FDragDropStyle>("drag.target", targetStyle2);
    tree.setTheme(other.get());
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    const auto            afterFills = spriteColors(after);
    ASSERT_EQ(afterFills.size(), 2u);
    EXPECT_EQ(afterFills[0], glm::vec4(0.9f, 0.8f, 0.1f, 1.0f));
    EXPECT_EQ(afterFills[1], glm::vec4(0.0f, 0.5f, 0.0f, 1.0f));
}

TEST(UIFrameSnapshotTest, SliceBrushImageIsSingleFullRect)
{
    const Rect2D dest{.pos = {10.0f, 20.0f}, .extent = {100.0f, 50.0f}};
    FBrushSlice  slices[kMaxBrushSlices];
    EXPECT_EQ(sliceBrush(FBrush::solid({1.0f, 0.0f, 0.0f, 1.0f}), dest, {32.0f, 32.0f}, slices), 1);
    EXPECT_EQ(slices[0].dest.pos, dest.pos);
    EXPECT_EQ(slices[0].dest.extent, dest.extent);
    EXPECT_EQ(slices[0].uvOffset, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(slices[0].uvScale, glm::vec2(1.0f, 1.0f));

    auto image = FBrush::image("tex");
    EXPECT_EQ(sliceBrush(image, dest, {32.0f, 32.0f}, slices), 1);
    EXPECT_EQ(slices[0].uvScale, glm::vec2(1.0f, 1.0f));
}

TEST(UIFrameSnapshotTest, SliceBrushNinePatchEmitsNineCells)
{
    const Rect2D dest{.pos = {0.0f, 0.0f}, .extent = {100.0f, 50.0f}};
    FBrushSlice  slices[kMaxBrushSlices];
    const auto   brush = FBrush::ninePatch("panel", {8.0f, 8.0f, 8.0f, 8.0f});
    ASSERT_EQ(sliceBrush(brush, dest, {32.0f, 32.0f}, slices), 9);

    EXPECT_EQ(slices[0].dest.pos, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(slices[0].dest.extent, glm::vec2(8.0f, 8.0f));
    EXPECT_EQ(slices[0].uvOffset, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(slices[0].uvScale, glm::vec2(0.25f, 0.25f));

    EXPECT_EQ(slices[4].dest.pos, glm::vec2(8.0f, 8.0f));
    EXPECT_EQ(slices[4].dest.extent, glm::vec2(84.0f, 34.0f));
    EXPECT_EQ(slices[4].uvOffset, glm::vec2(0.25f, 0.25f));
    EXPECT_EQ(slices[4].uvScale, glm::vec2(0.5f, 0.5f));

    EXPECT_EQ(slices[8].dest.pos, glm::vec2(92.0f, 42.0f));
    EXPECT_EQ(slices[8].dest.extent, glm::vec2(8.0f, 8.0f));
    EXPECT_EQ(slices[8].uvOffset, glm::vec2(0.75f, 0.75f));
    EXPECT_EQ(slices[8].uvScale, glm::vec2(0.25f, 0.25f));
}

TEST(UIFrameSnapshotTest, SliceBrushBorderOmitsCenter)
{
    const Rect2D dest{.pos = {0.0f, 0.0f}, .extent = {100.0f, 50.0f}};
    FBrushSlice  slices[kMaxBrushSlices];
    const auto   brush = FBrush::border("frame", {8.0f, 8.0f, 8.0f, 8.0f});
    ASSERT_EQ(sliceBrush(brush, dest, {32.0f, 32.0f}, slices), 8);
    for (int i = 0; i < 8; ++i) {
        EXPECT_FALSE(slices[i].dest.pos.x == 8.0f && slices[i].dest.pos.y == 8.0f &&
                     slices[i].dest.extent.x == 84.0f && slices[i].dest.extent.y == 34.0f);
    }
}

TEST(UIFrameSnapshotTest, SliceBrushScalesMarginsWhenDestIsSmaller)
{
    const Rect2D dest{.pos = {0.0f, 0.0f}, .extent = {10.0f, 10.0f}};
    FBrushSlice  slices[kMaxBrushSlices];
    const auto   brush = FBrush::ninePatch("panel", {8.0f, 8.0f, 8.0f, 8.0f});
    ASSERT_EQ(sliceBrush(brush, dest, {32.0f, 32.0f}, slices), 4);
    EXPECT_EQ(slices[0].dest.extent, glm::vec2(5.0f, 5.0f));
    EXPECT_EQ(slices[3].dest.pos, glm::vec2(5.0f, 5.0f));
    EXPECT_EQ(slices[3].dest.extent, glm::vec2(5.0f, 5.0f));
}

TEST(UIFrameSnapshotTest, AddBrushSolidStaysOneSprite)
{
    UIFrameBuilder builder(UIFrameBuildContext{});
    builder.addBrush(Rect2D{.pos = {10.0f, 10.0f}, .extent = {40.0f, 20.0f}},
                     FBrush::solid({0.2f, 0.3f, 0.4f, 1.0f}));
    const UIFrameSnapshot snap = builder.build({.width = 800, .height = 600});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_EQ(snap.items[0].color, glm::vec4(0.2f, 0.3f, 0.4f, 1.0f));
    EXPECT_EQ(snap.items[0].uvScale, glm::vec2(1.0f, 1.0f));
}

TEST(UIFrameSnapshotTest, AddBrushNinePatchWithoutTextureStretches)
{
    UIFrameBuilder builder(UIFrameBuildContext{});
    builder.addBrush(Rect2D{.pos = {0.0f, 0.0f}, .extent = {100.0f, 50.0f}},
                     FBrush::ninePatch("missing", {8.0f, 8.0f, 8.0f, 8.0f}));
    const UIFrameSnapshot snap = builder.build({.width = 800, .height = 600});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].uvScale, glm::vec2(1.0f, 1.0f));
}

} // namespace ya
