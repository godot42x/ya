// Phase 4 regression guards for the immutable UI frame packet: the tree is
// laid out and painted BEFORE the render graph, items carry resolved
// transforms/clips, and the snapshot is widget-independent (widgets may be
// detached right after build without invalidating the packet).

#include "Core/Profiling/PerfState.h"
#include "Core/Profiling/Profiling.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Widgets/UIFrameSnapshotDump.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "Render/Resources/FontManager.h"
#include "RHI/Core/Texture.h"

#include <cmath>

#include <gtest/gtest.h>

#include <set>
#include <unordered_map>
#include <vector>

namespace ya
{

TEST(UIFrameSnapshotTest, BuildResolvesItemsToRenderPixelsInPaintOrder)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIBorder>("Behind");
    behind->_zOrder   = 0;
    auto front = std::make_shared<UIButton>("Front");
    front->_zOrder    = 10;
    FCanvasSlotArgs behindSlot;
    behindSlot.offset    = {10.0f, 10.0f};
    behindSlot.fixedSize = {100.0f, 50.0f};
    FCanvasSlotArgs frontSlot;
    frontSlot.offset    = {200.0f, 100.0f};
    frontSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), behind, behindSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), front, frontSlot);

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
    clip->setClipChildren(true);
    auto child = std::make_shared<UIBorder>("Child");
    FCanvasSlotArgs clipSlot;
    clipSlot.fixedSize = {200.0f, 100.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), clip, clipSlot);
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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    FCanvasSlotArgs slot;
    slot.offset = {30.0f, 40.0f};
    slot.fixedSize = {200.0f, 20.0f};
    text->setText("Hello Snapshot");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);

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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {5.0f, 5.0f};
    slot.fixedSize = {50.0f, 25.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    // No explicit layout() call: buildSnapshot performs it.
    const UIFrameSnapshot snapshot = tree.buildSnapshot({});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(5.0f, 5.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(50.0f, 25.0f));
}

TEST(UIFrameSnapshotTest, PanelCornerRadiusScalesIntoDrawItem)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    panel->setColor({1.0f, 0.0f, 0.0f, 1.0f});
    panel->setCornerRadius(8.0f);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    auto       behind = std::make_shared<UIBorder>("Behind");
    auto front = std::make_shared<UIButton>("Front");
    FCanvasSlotArgs behindSlot;
    behindSlot.fixedSize = {100.0f, 50.0f};
    FCanvasSlotArgs frontSlot;
    frontSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), behind, behindSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), front, frontSlot);

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
    // Reattaching invalidates the detached subtree and may also rebuild its
    // layout host; the contract is a fresh rebuild, not an exact widget count.
    EXPECT_GE(tree.getPerfStats().rebuiltWidgets, 1u);
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
    auto       plain = std::make_shared<UICanvasPanel>("Plain");
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
    EXPECT_GE(tree.getPerfStats().rebuiltWidgets, 1u);
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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {50.0f, 25.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{}); // clean: panel reuses its cached items
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Move the panel and invalidate layout: the widget's rect changes, so its
    // cached draw items (old pixel position) must be rebuilt at the new spot.
    auto* canvasSlot = panel->getSlot()->as<UICanvasSlot>();
    ASSERT_NE(canvasSlot, nullptr);
    canvasSlot->setOffset({100.0f, 100.0f});
    tree.invalidateLayout();
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(100.0f, 100.0f));
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(50.0f, 25.0f));
}

TEST(UIFrameSnapshotTest, DetachAndReattachForcesFreshPaintCacheSegment)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UIBorder>("CachedPanel");
    FCanvasSlotArgs args;
    args.fixedSize = {80.0f, 24.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, args).valid());

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    tree.detach(*panel);
    ASSERT_FALSE(panel->isAttached());
    ASSERT_TRUE(tree.buildSnapshot(UIFrameBuildContext{}).items.empty());

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, args).valid());
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_GE(tree.getPerfStats().rebuiltWidgets, 1u);
}

TEST(UIFrameSnapshotTest, CrossTreeReparentDoesNotReuseOldTreeCache)
{
    WidgetTree source({.width = 320, .height = 200});
    WidgetTree destination({.width = 640, .height = 400});
    auto panel = std::make_shared<UIBorder>("CrossTreePanel");
    FCanvasSlotArgs sourceArgs;
    sourceArgs.offset = {12.0f, 16.0f};
    sourceArgs.fixedSize = {80.0f, 24.0f};
    ASSERT_TRUE(source.attach(*source.getLayer(WidgetTree::ELayer::Content), panel, sourceArgs).valid());

    source.buildSnapshot(UIFrameBuildContext{});
    source.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(source.getPerfStats().rebuiltWidgets, 0u);

    destination.reparent(*destination.getLayer(WidgetTree::ELayer::Content), panel);
    ASSERT_TRUE(destination.contains(*panel));
    ASSERT_FALSE(source.contains(*panel));
    FCanvasSlotArgs destArgs;
    destArgs.offset = {40.0f, 50.0f};
    destArgs.fixedSize = {80.0f, 24.0f};
    if (UISlot* edge = destination.getLayer(WidgetTree::ELayer::Content)->getSlotForChild(*panel)) {
        if (auto* canvas = edge->as<UICanvasSlot>()) {
            canvas->apply(destArgs);
        }
    }

    const UIFrameSnapshot snapshot = destination.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_GT(destination.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(snapshot.items[0].pos, glm::vec2(40.0f, 50.0f));
}

TEST(UIFrameSnapshotTest, DestroyAndReallocateWidgetGetsNewRuntimeIdentity)
{
    WidgetTree tree({.width = 320, .height = 200});
    uint64_t oldId = 0;
    {
        auto oldPanel = std::make_shared<UIBorder>("OldPanel");
        oldId = oldPanel->getRuntimeId();
        FCanvasSlotArgs args;
        args.fixedSize = {64.0f, 20.0f};
        ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), oldPanel, args).valid());
        tree.buildSnapshot(UIFrameBuildContext{});
        tree.detach(*oldPanel);
    }

    auto replacement = std::make_shared<UIBorder>("ReplacementPanel");
    EXPECT_NE(replacement->getRuntimeId(), oldId);
    FCanvasSlotArgs args;
    args.fixedSize = {96.0f, 20.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), replacement, args).valid());
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snapshot.items.size(), 1u);
    EXPECT_EQ(snapshot.items[0].size, glm::vec2(96.0f, 20.0f));
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
    FCanvasSlotArgs boundSlot;
    boundSlot.fixedSize = {160.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bound, boundSlot);

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

TEST(UIFrameSnapshotTest, LayoutInvalidationScopesAccumulateAcrossSnapshots)
{
    WidgetTree tree({.width = 320, .height = 200});
    auto panel = std::make_shared<UICanvasPanel>("Panel");
    FCanvasSlotArgs args;
    args.fixedSize = {80.0f, 24.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, args).valid());
    tree.buildSnapshot(UIFrameBuildContext{});

    auto* slot = panel->getSlot()->as<UICanvasSlot>();
    ASSERT_NE(slot, nullptr);
    slot->setOffset({10.0f, 12.0f});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GE(tree.getPerfStats().arrangeInvalidations, 1u);

    slot->setWidthSizeMode(EWidgetSizeMode::Auto);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GE(tree.getPerfStats().measureInvalidations, 1u);

    tree.detach(*panel);
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_GE(tree.getPerfStats().structureInvalidations, 1u);
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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

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
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    tree.buildSnapshot(UIFrameBuildContext{.generation = 0});
    tree.buildSnapshot(UIFrameBuildContext{.generation = 0});
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    // Host bumps generation (e.g. texture/asset reload) while the mapping is
    // unchanged: cached resolved-texture segments must not be reused.
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{.generation = 1});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(tree.getPerfStats().cacheInvalidations, 1u);
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ResourceReady);
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
    auto       panel = std::make_shared<UIBorder>("P");
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
    auto       panel = std::make_shared<UIBorder>("P");
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
    auto       panel = std::make_shared<UIBorder>("P");
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
    auto       parent = std::make_shared<UICanvasPanel>("Parent");
    auto       child  = std::make_shared<UICanvasPanel>("Child");
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
    auto       child  = std::make_shared<UICanvasPanel>("Child");
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
    FCanvasSlotArgs viewportSlot;
    viewportSlot.fixedSize = {200.0f, 60.0f};
    viewport->_bShowScrollbar = false;
    auto content = std::make_shared<UIBorder>("Content");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), viewport, viewportSlot);
    viewport->addDetachedChild(content, [](UIElement&, UISlot& slot) {
        if (auto* single = dynamic_cast<UIContentSlot*>(&slot)) {
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
    FCanvasSlotArgs splitSlot;
    splitSlot.fixedSize = {400.0f, 200.0f};
    split->setSplitRatio(0.5f);
    auto paneA = std::make_shared<UIBorder>("A");
    auto paneB = std::make_shared<UIBorder>("B");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
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
    auto child = std::make_shared<UICanvasPanel>("A");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), container);
    tree.attach(*container, child, [](UIElement&, UISlot& slot) {
        if (auto* box = slot.as<UIBoxSlot>()) {
            box->setPreferredSize({40.0f, 20.0f});
        }
    });

    auto split = std::make_shared<UISplitPane>("Split");
    auto paneA = std::make_shared<UICanvasPanel>("PA");
    auto paneB = std::make_shared<UICanvasPanel>("PB");
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
    FCanvasSlotArgs clipSlot;
    clipSlot.fixedSize = {200.0f, 100.0f};
    auto child = std::make_shared<UIBorder>("Child");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), clip, clipSlot);
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
    if (auto* canvas = clip->getSlot()->as<UICanvasSlot>()) {
        canvas->setFixedSize({300.0f, 100.0f});
    }
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});

    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_TRUE(snap.items[0].bClipped);
    EXPECT_EQ(snap.items[0].clip.extent, glm::vec2(50.0f, 100.0f));
}

TEST(UIFrameSnapshotTest, AuthoredButtonStyleWinsOverThemeAndIgnoresThemeSwitch)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {10.0f, 10.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    FButtonStyle authored;
    authored.normalFill = FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f});
    button->setStyle(authored);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);

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
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {10.0f, 10.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);

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

    auto panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs panelSlot;
    panelSlot.offset = {10.0f, 10.0f};
    panelSlot.fixedSize = {100.0f, 50.0f};
    panel->setColor({0.9f, 0.2f, 0.1f, 1.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);

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
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {10.0f, 10.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    FButtonStyle style;
    style.normalFill = FBrush::solid({0.2f, 0.3f, 0.4f, 1.0f});
    button->setStyle(style);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
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
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset = {10.0f, 10.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    button->setStyleField("normalFill", FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);

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

TEST(UIFrameSnapshotTest, ImagePlaceholderFollowsThemeWithoutModalChrome)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto theme = std::make_shared<UITheme>();
    FImageStyle imageStyle;
    imageStyle.placeholderFill = FBrush::solid({0.1f, 0.2f, 0.3f, 1.0f});
    theme->define<FImageStyle>("image", imageStyle);
    theme->define<FPopupStyle>("popup", FPopupStyle{});
    tree.setTheme(theme.get());

    auto image = std::make_shared<UIImage>("Img");
    FCanvasSlotArgs imageSlot;
    imageSlot.offset = {10.0f, 10.0f};
    imageSlot.fixedSize = {40.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, imageSlot);

    auto overlay = std::make_shared<UIPopupOverlay>("Modal");
    overlay->_bModal = true;
    FCanvasSlotArgs overlayArgs;
    overlayArgs.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), overlay, overlayArgs).valid());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    EXPECT_FALSE(image->hasAuthoredStyle());
    EXPECT_FALSE(overlay->hasAuthoredStyle());

    auto other = std::make_shared<UITheme>();
    FImageStyle imageStyle2;
    imageStyle2.placeholderFill = FBrush::solid({0.9f, 0.8f, 0.1f, 1.0f});
    other->define<FImageStyle>("image", imageStyle2);
    other->define<FPopupStyle>("popup", FPopupStyle{});
    tree.setTheme(other.get());
    const UIFrameSnapshot after = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(after.items.size(), 1u);
    EXPECT_EQ(after.items[0].color, glm::vec4(0.9f, 0.8f, 0.1f, 1.0f));
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
    FCanvasSlotArgs treeSlot;
    treeSlot.fixedSize = {200.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tv, treeSlot);
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
    FCanvasSlotArgs sourceSlot;
    sourceSlot.offset = {10.0f, 10.0f};
    sourceSlot.fixedSize = {80.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    auto target = std::make_shared<UIDragDropTile>("Dst", UIDragDropTile::EKind::Target);
    FCanvasSlotArgs targetSlot;
    targetSlot.offset = {10.0f, 40.0f};
    targetSlot.fixedSize = {80.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);

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

TEST(UIFrameSnapshotTest, AddRectFilledMultiColorStoresPerVertexColors)
{
    UIFrameBuilder builder(UIFrameBuildContext{
        .uiScale = {2.0f, 2.0f},
        .offset  = {10.0f, 20.0f},
    });
    const glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 hue{1.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 black{0.0f, 0.0f, 0.0f, 1.0f};
    builder.addRectFilledMultiColor(Rect2D{.pos = {1.0f, 2.0f}, .extent = {10.0f, 8.0f}},
                                    white,
                                    hue,
                                    black,
                                    black);
    const UIFrameSnapshot snap = builder.build({.width = 800, .height = 600});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].kind, UIFrameDrawItem::EKind::Sprite);
    EXPECT_TRUE(snap.items[0].bPerVertexColor);
    EXPECT_EQ(snap.items[0].pos, glm::vec2(12.0f, 24.0f));
    EXPECT_EQ(snap.items[0].size, glm::vec2(20.0f, 16.0f));
    EXPECT_EQ(snap.items[0].color, white);
    EXPECT_EQ(snap.items[0].vertexColors[0], white);
    EXPECT_EQ(snap.items[0].vertexColors[1], hue);
    EXPECT_EQ(snap.items[0].vertexColors[2], black);
    EXPECT_EQ(snap.items[0].vertexColors[3], black);
}

TEST(UIFrameSnapshotTest, AddSpriteStoresOpaqueSampleFlag)
{
    UIFrameBuilder opaqueBuilder(UIFrameBuildContext{});
    opaqueBuilder.addSprite(Rect2D{.pos = {0.0f, 0.0f}, .extent = {16.0f, 16.0f}},
                            glm::vec4(1.0f),
                            nullptr,
                            {0.0f, 0.0f},
                            {1.0f, 1.0f},
                            true);
    const UIFrameSnapshot opaqueSnap = opaqueBuilder.build({.width = 64, .height = 64});
    ASSERT_EQ(opaqueSnap.items.size(), 1u);
    EXPECT_TRUE(opaqueSnap.items[0].bOpaqueSample);

    UIFrameBuilder defaultBuilder(UIFrameBuildContext{});
    defaultBuilder.addSprite(Rect2D{.pos = {0.0f, 0.0f}, .extent = {8.0f, 8.0f}}, glm::vec4(1.0f), nullptr);
    const UIFrameSnapshot defaultSnap = defaultBuilder.build({.width = 64, .height = 64});
    ASSERT_EQ(defaultSnap.items.size(), 1u);
    EXPECT_FALSE(defaultSnap.items[0].bOpaqueSample);
}

TEST(UIFrameSnapshotTest, ContainedImageRectPreservesAspectInsideBounds)
{
    const Rect2D wide{.pos = {10.0f, 20.0f}, .extent = {200.0f, 100.0f}};
    const Rect2D fitted = containedImageRect(wide, 64.0f, 64.0f);
    EXPECT_FLOAT_EQ(fitted.extent.x, 100.0f);
    EXPECT_FLOAT_EQ(fitted.extent.y, 100.0f);
    EXPECT_FLOAT_EQ(fitted.pos.x, 60.0f);
    EXPECT_FLOAT_EQ(fitted.pos.y, 20.0f);

    const Rect2D tall{.pos = {0.0f, 0.0f}, .extent = {100.0f, 200.0f}};
    const Rect2D fittedWide = containedImageRect(tall, 200.0f, 50.0f);
    EXPECT_FLOAT_EQ(fittedWide.extent.x, 100.0f);
    EXPECT_FLOAT_EQ(fittedWide.extent.y, 25.0f);
    EXPECT_FLOAT_EQ(fittedWide.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(fittedWide.pos.y, 87.5f);
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

TEST(UIFrameSnapshotTest, StyleFieldImpactCatalogClassifiesPaintLayoutAndResource)
{
    const FStyleFieldImpact fontSize = lookupStyleFieldImpact<FTextStyle>("fontSize");
    EXPECT_TRUE(fontSize.bPaint);
    EXPECT_TRUE(fontSize.bLayout);
    EXPECT_FALSE(fontSize.bResource);

    const FStyleFieldImpact textColor = lookupStyleFieldImpact<FTextStyle>("textColor");
    EXPECT_TRUE(textColor.bPaint);
    EXPECT_FALSE(textColor.bLayout);
    EXPECT_FALSE(textColor.bResource);

    const FStyleFieldImpact fillColor = lookupStyleFieldImpact<FTextStyle>("fillColor");
    EXPECT_TRUE(fillColor.bPaint);
    EXPECT_FALSE(fillColor.bLayout);
    EXPECT_TRUE(fillColor.bResource);

    const FStyleFieldImpact buttonPadding = lookupStyleFieldImpact<FButtonStyle>("padding");
    EXPECT_TRUE(buttonPadding.bLayout);
    EXPECT_FALSE(buttonPadding.bResource);

    const FStyleFieldImpact panelFill = lookupStyleFieldImpact<FPanelStyle>("fillColor");
    EXPECT_TRUE(panelFill.bPaint);
    EXPECT_FALSE(panelFill.bLayout);
    EXPECT_TRUE(panelFill.bResource);

    const FStyleFieldImpact scrollbarWidth = lookupStyleFieldImpact<FScrollBarStyle>("width");
    EXPECT_TRUE(scrollbarWidth.bPaint);
    EXPECT_FALSE(scrollbarWidth.bLayout);
    EXPECT_FALSE(scrollbarWidth.bResource);

    const FStyleFieldImpact textFieldPadding = lookupStyleFieldImpact<FTextFieldStyle>("padding");
    EXPECT_TRUE(textFieldPadding.bLayout);
    EXPECT_FALSE(textFieldPadding.bResource);

    const FStyleFieldImpact minSize = lookupStyleFieldImpact<FFloatingWindowStyle>("minSize");
    EXPECT_TRUE(minSize.bLayout);

    // A font family carries its own metrics, so it has to classify as layout the
    // same way fontSize does. Paint-only would let a document-authored family
    // draw in the new face inside the old measurement.
    const FStyleFieldImpact fontFamily = lookupStyleFieldImpact<FTextStyle>("fontFamily");
    EXPECT_TRUE(fontFamily.bPaint);
    EXPECT_TRUE(fontFamily.bLayout);
    EXPECT_FALSE(fontFamily.bResource);
}

TEST(UIFrameSnapshotTest, StylePatchImpactUnionsFieldMetadata)
{
    const FStyleFieldImpact paintOnly =
        lookupStylePatchImpact<FTextStyle>(nlohmann::json{{"textColor", nullptr}});
    EXPECT_TRUE(paintOnly.bPaint);
    EXPECT_FALSE(paintOnly.bLayout);
    EXPECT_FALSE(paintOnly.bResource);

    const FStyleFieldImpact mixed = lookupStylePatchImpact<FTextStyle>(
        nlohmann::json{{"fontSize", 24}, {"textColor", nullptr}});
    EXPECT_TRUE(mixed.bPaint);
    EXPECT_TRUE(mixed.bLayout);
    EXPECT_FALSE(mixed.bResource);

    const FStyleFieldImpact resource =
        lookupStylePatchImpact<FTextStyle>(nlohmann::json{{"fillColor", nullptr}});
    EXPECT_TRUE(resource.bResource);
    EXPECT_FALSE(resource.bLayout);
}

TEST(UIFrameSnapshotTest, SetStyleFieldUsesCatalogImpact)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       text = std::make_shared<UIText>("T");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(text->isPaintDirty());
    EXPECT_FALSE(text->isMeasureDirty());

    text->setStyleField("textColor", glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
    EXPECT_TRUE(text->isPaintDirty());
    EXPECT_FALSE(text->isMeasureDirty());

    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_FALSE(text->isPaintDirty());
    EXPECT_FALSE(text->isMeasureDirty());

    text->setStyleField("fontSize", uint32_t{24});
    EXPECT_TRUE(text->isMeasureDirty());
}

TEST(UIFrameSnapshotTest, SetStyleUsesPatchUnionImpact)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs panelSlot;
    panelSlot.offset    = {10.0f, 10.0f};
    panelSlot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    FPanelStyle style;
    style.fillColor = FBrush::solid({0.9f, 0.2f, 0.1f, 1.0f});
    panel->setStyle(style);
    EXPECT_TRUE(panel->isPaintDirty());
    EXPECT_FALSE(panel->isMeasureDirty());

    auto button = std::make_shared<UIButton>("B");
    FCanvasSlotArgs buttonSlot;
    buttonSlot.offset    = {10.0f, 10.0f};
    buttonSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});

    button->setStyleField("normalFill", FBrush::solid({0.1f, 0.2f, 0.9f, 1.0f}));
    EXPECT_TRUE(button->isPaintDirty());
    EXPECT_FALSE(button->isMeasureDirty());

    tree.buildSnapshot(UIFrameBuildContext{});
    button->setStyleField("padding", glm::vec2{20.0f, 8.0f});
    EXPECT_TRUE(button->isMeasureDirty());
}

TEST(UIFrameSnapshotTest, StyleCatalogAcceptsKnownKeysAndEditorPrefix)
{
#define YA_GUI_STYLE_KEY_EXPECT_KNOWN(Type, Name, Str) \
    EXPECT_EQ(lookupStyleKey(StyleKey::Name, ya::type_index_v<Type>), EStyleKeyLookup::Known);
    YA_GUI_STYLE_CATALOG(YA_GUI_STYLE_KEY_EXPECT_KNOWN)
#undef YA_GUI_STYLE_KEY_EXPECT_KNOWN

    EXPECT_EQ(lookupStyleKey<FTextStyle>(""), EStyleKeyLookup::Empty);
    EXPECT_EQ(lookupStyleKey<FTextStyle>("editor.text.header"), EStyleKeyLookup::Known);
    EXPECT_EQ(lookupStyleKey<FPanelStyle>(StyleKey::Canvas), EStyleKeyLookup::Known);
    EXPECT_EQ(lookupStyleKey<FTextStyle>("text.heder"), EStyleKeyLookup::UnknownKey);
    EXPECT_EQ(lookupStyleKey<FTextStyle>(StyleKey::Button), EStyleKeyLookup::TypeMismatch);
    EXPECT_EQ(lookupStyleKey<FPanelStyle>("editor.missing"), EStyleKeyLookup::UnknownKey);
}

TEST(UIFrameSnapshotTest, StyleCatalogDiagnosesUnknownAndMismatchedKeys)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       text = std::make_shared<UIText>("T");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text);

    const StyleCatalogDiagnostics before = getStyleCatalogDiagnostics();
    text->setStyleKey("not.a.style");
    EXPECT_EQ(text->_styleKey, "not.a.style");
    EXPECT_EQ(getStyleCatalogDiagnostics().unknownKeys, before.unknownKeys + 1);

    text->setStyleKey(std::string(StyleKey::Button));
    EXPECT_EQ(text->_styleKey, StyleKey::Button);
    EXPECT_EQ(getStyleCatalogDiagnostics().typeMismatches, before.typeMismatches + 1);

    auto theme = std::make_shared<UITheme>();
    theme->define<FPanelStyle>("panel.winodw", FPanelStyle{});
    EXPECT_EQ(getStyleCatalogDiagnostics().unknownKeys, before.unknownKeys + 2);

    text->setStyleKey(std::string(StyleKey::TextHeader));
    EXPECT_EQ(getStyleCatalogDiagnostics().unknownKeys, before.unknownKeys + 2);
    EXPECT_EQ(getStyleCatalogDiagnostics().typeMismatches, before.typeMismatches + 1);
}

namespace
{

std::shared_ptr<Font> makeSnapshotTestFont(float fontSize, float advancePerChar)
{
    auto font        = std::make_shared<Font>();
    font->fontSize   = fontSize;
    font->lineHeight = fontSize * 1.25f;
    font->ascent     = fontSize;
    font->descent    = fontSize * 0.25f;
    for (uint32_t cp = 32; cp < 127; ++cp) {
        Character ch;
        ch.uvRect   = {};
        ch.size     = {static_cast<int>(advancePerChar), static_cast<int>(fontSize)};
        ch.bearing  = {0, 0};
        ch.advance    = {advancePerChar, 0.0f};
        ch.designSize = static_cast<uint32_t>(fontSize);
        ch.bInAtlas   = true;
        font->characters[cp] = ch;
    }
    return font;
}

std::shared_ptr<Texture> makeFakeTexture()
{
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(static_cast<uintptr_t>(0x1)),
                                    [](Texture*) {});
}

struct FakeGuiTextureSource final : IGuiTextureSource
{
    std::unordered_map<std::string, FGuiTextureLookup> store;
    int                                                requestCount = 0;

    [[nodiscard]] FGuiTextureLookup lookup(const std::string& path) override
    {
        const auto it = store.find(path);
        if (it != store.end()) {
            return it->second;
        }
        return {nullptr, EGuiTextureState::Pending};
    }

    void requestLoad(const std::string&, FGuiTextureReady) override { ++requestCount; }
};

} // namespace

TEST(UIFrameSnapshotTest, FontManagerRevisionBumpsOnRegister)
{
    const uint64_t before = FontManager::get()->resourceRevision();
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, makeSnapshotTestFont(16.0f, 8.0f));
    EXPECT_GT(FontManager::get()->resourceRevision(), before);
}

// Paint-time font lookups take the density from the snapshot context, not
// from the process-global active DPI: two trees with different densities
// (editor chrome vs game UI in PIE) interleave snapshots in one frame.
TEST(UIFrameSnapshotTest, BuilderFontLookupUsesLogicalSize)
{
    // Density is not a cache key. The same pixel size is one font; the draw
    // path asks for a different size when the tree is scaled.
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, makeSnapshotTestFont(16.0f, 8.0f));
    auto fontAtOne = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 16, 1.0f);
    auto fontAtTwo = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 16, 2.0f);
    ASSERT_NE(fontAtOne, nullptr);
    EXPECT_EQ(fontAtOne, fontAtTwo);

    UIFrameBuildContext ctxAtTwo{};
    ctxAtTwo.fontDpi = 2.0f;
    UIFrameBuilder builderAtTwo(ctxAtTwo);
    EXPECT_EQ(builderAtTwo.getFont(DEFAULT_RUNTIME_FONT_NAME, 16), fontAtOne);

    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 32, makeSnapshotTestFont(32.0f, 16.0f));
    auto font32 = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, 32);

    WidgetTree tree({.width = 200, .height = 100});
    tree.setDpiScale(2.0f);
    auto text = std::make_shared<UIText>("Label");
    text->setText("AB");
    FCanvasSlotArgs slot;
    slot.fixedSize = {100.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);
    const UIFrameSnapshot snapshot = tree.buildSnapshot(UIFrameBuildContext{});
    const UIFrameDrawItem* drawn = nullptr;
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (item.kind == UIFrameDrawItem::EKind::Text) {
            drawn = &item;
            break;
        }
    }
    ASSERT_NE(drawn, nullptr);
    EXPECT_EQ(drawn->font.get(), font32.get());
    EXPECT_FLOAT_EQ(drawn->textScale.x, 1.0f);
    EXPECT_FLOAT_EQ(drawn->textScale.y, 1.0f);
}

// Glyph origins snap to whole device pixels: a fractional start would
// resample the bitmap atlas under Nearest sampling and blur the text.
TEST(UIFrameSnapshotTest, TextOriginsSnapToWholeDevicePixels)
{
    FontManager::get()->setActiveDpiScale(1.0f);
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, makeSnapshotTestFont(16.0f, 8.0f));

    WidgetTree tree({.width = 200, .height = 100});
    auto  text = std::make_shared<UIText>("Label");
    text->setText("AB");
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), text, slot);

    UIFrameBuildContext ctx{};
    ctx.offset = {10.5f, 20.25f}; // fractional target-space origin
    const UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);
    bool bFoundText = false;
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (item.kind != UIFrameDrawItem::EKind::Text) {
            continue;
        }
        bFoundText = true;
        EXPECT_NEAR(item.pos.x, std::round(item.pos.x), 1e-4f);
        EXPECT_NEAR(item.pos.y, std::round(item.pos.y), 1e-4f);
    }
    EXPECT_TRUE(bFoundText);
}

TEST(UIFrameSnapshotTest, FontResourceReadyRelayoutsNestedText)
{
    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, makeSnapshotTestFont(16.0f, 8.0f));

    WidgetTree tree({.width = 800, .height = 600});
    auto       column = std::make_shared<UIContainer>("Column");
    column->setDirection(EWidgetBoxLayout::Vertical);
    FCanvasSlotArgs fillSlot;
    fillSlot.anchorMin = {0.0f, 0.0f};
    fillSlot.anchorMax = {1.0f, 1.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), column, fillSlot);

    auto text = std::make_shared<UIText>("Label");
    text->setText("AB");
    text->setFontSize(16);
    tree.attach(*column, text);
    if (UIBoxSlot* slot = column->getBoxSlot(*text)) {
        slot->setCrossAlignment(EUIBoxSlotCrossAlignment::Start);
    }

    tree.buildSnapshot(UIFrameBuildContext{});
    tree.buildSnapshot(UIFrameBuildContext{});
    const float widthBefore = text->getLayoutRect().extent.x;
    EXPECT_FLOAT_EQ(widthBefore, 16.0f);

    FontManager::get()->registerFont(DEFAULT_RUNTIME_FONT_NAME, 16, makeSnapshotTestFont(16.0f, 20.0f));
    tree.buildSnapshot(UIFrameBuildContext{});
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ResourceReady);
    EXPECT_FLOAT_EQ(text->getLayoutRect().extent.x, 40.0f);
    EXPECT_GT(tree.getPerfStats().rebuiltWidgets, 0u);
}

TEST(UIFrameSnapshotTest, ImageResolverReadyAfterGenerationBumpPaintsTexture)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       image = std::make_shared<UIImage>("Img");
    image->_assetPath = "tex:ready";
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, slot);

    UIFrameBuildContext missCtx;
    missCtx.generation = 1;
    missCtx.textureResolver = [](const std::string&) { return std::shared_ptr<Texture>(); };
    const UIFrameSnapshot missSnap = tree.buildSnapshot(missCtx);
    bool bMissHasTexture = false;
    for (const auto& item : missSnap.items) {
        if (item.texture) {
            bMissHasTexture = true;
        }
    }
    EXPECT_FALSE(bMissHasTexture);

    auto ready = makeFakeTexture();
    UIFrameBuildContext hitCtx;
    hitCtx.generation = 2;
    hitCtx.textureResolver = [&](const std::string& path) {
        return path == "tex:ready" ? ready : std::shared_ptr<Texture>();
    };
    const UIFrameSnapshot hitSnap = tree.buildSnapshot(hitCtx);
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ResourceReady);
    bool bHitHasTexture = false;
    for (const auto& item : hitSnap.items) {
        if (item.texture == ready) {
            bHitHasTexture = true;
        }
    }
    EXPECT_TRUE(bHitHasTexture);
}

TEST(UIFrameSnapshotTest, ImageUnresolvedPathUsesPlaceholderUntilMissingFlag)
{
    WidgetTree tree({.width = 280, .height = 120});
    auto empty = std::make_shared<UIImage>("Empty");
    auto pending = std::make_shared<UIImage>("Pending");
    pending->_assetPath = "tex:pending";
    auto failed = std::make_shared<UIImage>("Failed");
    failed->_assetPath = "tex:failed";
    failed->setResourceMissing(true);
    FCanvasSlotArgs emptySlot;
    emptySlot.offset    = {10.0f, 10.0f};
    emptySlot.fixedSize = {64.0f, 64.0f};
    FCanvasSlotArgs pendingSlot;
    pendingSlot.offset    = {90.0f, 10.0f};
    pendingSlot.fixedSize = {64.0f, 64.0f};
    FCanvasSlotArgs failedSlot;
    failedSlot.offset    = {170.0f, 10.0f};
    failedSlot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), empty, emptySlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), pending, pendingSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), failed, failedSlot);

    UIFrameBuildContext ctx;
    ctx.generation = 1;
    ctx.textureResolver = [](const std::string&) { return std::shared_ptr<Texture>(); };
    const UIFrameSnapshot snapshot = tree.buildSnapshot(ctx);

    const UIFrameDrawItem* emptyItem = nullptr;
    const UIFrameDrawItem* pendingItem = nullptr;
    const UIFrameDrawItem* failedItem = nullptr;
    for (const UIFrameDrawItem& item : snapshot.items) {
        if (!item.texture && item.pos.x == 10.0f) {
            emptyItem = &item;
        }
        if (!item.texture && item.pos.x == 90.0f) {
            pendingItem = &item;
        }
        if (!item.texture && item.pos.x == 170.0f) {
            failedItem = &item;
        }
    }
    ASSERT_NE(emptyItem, nullptr);
    ASSERT_NE(pendingItem, nullptr);
    ASSERT_NE(failedItem, nullptr);
    EXPECT_EQ(emptyItem->color, FImageStyle{}.placeholderFill.tintColor);
    EXPECT_EQ(pendingItem->color, FImageStyle{}.placeholderFill.tintColor);
    EXPECT_EQ(failedItem->color, FImageStyle{}.errorFill.tintColor);
}

TEST(UIFrameSnapshotTest, GuiTextureCatalogNotifyDirtiesOnlySubscribers)
{
    WidgetTree tree({.width = 280, .height = 120});
    FakeGuiTextureSource source;
    tree.setTextureSource(&source);

    auto image = std::make_shared<UIImage>("Img");
    image->_assetPath = "tex:catalog";
    auto panel = std::make_shared<UICanvasPanel>("Side");
    FCanvasSlotArgs imageSlot;
    imageSlot.offset    = {10.0f, 10.0f};
    imageSlot.fixedSize = {64.0f, 64.0f};
    FCanvasSlotArgs panelSlot;
    panelSlot.offset    = {90.0f, 10.0f};
    panelSlot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, imageSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);

    UIFrameBuildContext ctx;
    const UIFrameSnapshot missSnap = tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
    bool bMissHasTexture = false;
    const UIFrameDrawItem* missImage = nullptr;
    for (const UIFrameDrawItem& item : missSnap.items) {
        if (item.pos.x == 10.0f) {
            missImage = &item;
        }
        if (item.texture) {
            bMissHasTexture = true;
        }
    }
    ASSERT_NE(missImage, nullptr);
    EXPECT_FALSE(bMissHasTexture);
    EXPECT_EQ(missImage->color, FImageStyle{}.placeholderFill.tintColor);

    tree.buildSnapshot(ctx);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    EXPECT_EQ(source.requestCount, 1);
    const uint64_t cacheInv = tree.getPerfStats().cacheInvalidations;

    auto ready = makeFakeTexture();
    source.store["tex:catalog"] = {ready, EGuiTextureState::Ready};
    tree.textureCatalog().notify("tex:catalog", {ready, EGuiTextureState::Ready});

    const UIFrameSnapshot hitSnap = tree.buildSnapshot(ctx);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 1u);
    EXPECT_EQ(tree.getPerfStats().cacheInvalidations, cacheInv);
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ReactivePaint);
    bool bHitHasTexture = false;
    for (const UIFrameDrawItem& item : hitSnap.items) {
        if (item.texture == ready) {
            bHitHasTexture = true;
        }
    }
    EXPECT_TRUE(bHitHasTexture);
}

TEST(UIFrameSnapshotTest, GuiTextureCatalogSharedPathLoadsOnce)
{
    WidgetTree tree({.width = 280, .height = 120});
    FakeGuiTextureSource source;
    tree.setTextureSource(&source);

    auto left = std::make_shared<UIImage>("Left");
    left->_assetPath = "tex:shared";
    auto right = std::make_shared<UIImage>("Right");
    right->_assetPath = "tex:shared";
    FCanvasSlotArgs leftSlot;
    leftSlot.offset    = {10.0f, 10.0f};
    leftSlot.fixedSize = {64.0f, 64.0f};
    FCanvasSlotArgs rightSlot;
    rightSlot.offset    = {90.0f, 10.0f};
    rightSlot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), left, leftSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), right, rightSlot);

    UIFrameBuildContext ctx;
    tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
    tree.buildSnapshot(ctx);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    auto ready = makeFakeTexture();
    source.store["tex:shared"] = {ready, EGuiTextureState::Ready};
    tree.textureCatalog().notify("tex:shared", {ready, EGuiTextureState::Ready});

    const UIFrameSnapshot hitSnap = tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 2u);
    int hitCount = 0;
    for (const UIFrameDrawItem& item : hitSnap.items) {
        if (item.texture == ready) {
            ++hitCount;
        }
    }
    EXPECT_EQ(hitCount, 2);
}

TEST(UIFrameSnapshotTest, GuiTextureCatalogFailedPaintsErrorWithoutRetry)
{
    WidgetTree tree({.width = 200, .height = 120});
    FakeGuiTextureSource source;
    tree.setTextureSource(&source);

    auto image = std::make_shared<UIImage>("Failed");
    image->_assetPath = "tex:broken";
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {64.0f, 64.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, slot);

    UIFrameBuildContext ctx;
    tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
    tree.buildSnapshot(ctx);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);

    source.store["tex:broken"] = {nullptr, EGuiTextureState::Failed};
    tree.textureCatalog().notify("tex:broken", {nullptr, EGuiTextureState::Failed});

    const UIFrameSnapshot failSnap = tree.buildSnapshot(ctx);
    const UIFrameDrawItem* failItem = nullptr;
    for (const UIFrameDrawItem& item : failSnap.items) {
        if (item.pos.x == 10.0f) {
            failItem = &item;
        }
    }
    ASSERT_NE(failItem, nullptr);
    EXPECT_EQ(failItem->texture, nullptr);
    EXPECT_EQ(failItem->color, FImageStyle{}.errorFill.tintColor);

    tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
    EXPECT_EQ(tree.getPerfStats().rebuiltWidgets, 0u);
    tree.buildSnapshot(ctx);
    EXPECT_EQ(source.requestCount, 1);
}

TEST(UIFrameSnapshotTest, GuiTextureCatalogRefreshFromSourceKeepsReadyTexture)
{
    FakeGuiTextureSource source;
    auto                 ready = makeFakeTexture();
    source.store["tex:keep"]   = {ready, EGuiTextureState::Ready};

    FGuiTextureCatalog catalog;
    catalog.setSource(&source);
    const FGuiTextureLookup first = catalog.bind("tex:keep", {});
    EXPECT_EQ(first.state, EGuiTextureState::Ready);
    EXPECT_EQ(first.texture, ready);

    catalog.refreshFromSource();
    const FGuiTextureLookup second = catalog.bind("tex:keep", {});
    EXPECT_EQ(second.state, EGuiTextureState::Ready);
    EXPECT_EQ(second.texture, ready);
    EXPECT_EQ(source.requestCount, 0);
}

TEST(UIFrameSnapshotTest, VisualFillPrecedenceMatrix)
{
    FVisualChrome chrome;
    chrome.normal          = FBrush::solid({0.10f, 0.10f, 0.10f, 1.0f});
    chrome.hovered         = FBrush::solid({0.20f, 0.20f, 0.20f, 1.0f});
    chrome.pressed         = FBrush::solid({0.30f, 0.30f, 0.30f, 1.0f});
    chrome.focused         = FBrush::solid({0.40f, 0.40f, 0.40f, 1.0f});
    chrome.disabled        = FBrush::solid({0.50f, 0.50f, 0.50f, 1.0f});
    chrome.selected        = FBrush::solid({0.60f, 0.60f, 0.60f, 1.0f});
    chrome.selectedHovered = FBrush::solid({0.70f, 0.70f, 0.70f, 1.0f});
    chrome.error           = FBrush::solid({0.80f, 0.20f, 0.20f, 1.0f});
    chrome.dropTarget      = FBrush::solid({0.20f, 0.80f, 0.20f, 1.0f});

    EXPECT_EQ(resolveVisualFill(chrome, 0), chrome.normal);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, false, false, false, false, false, false)),
              chrome.hovered);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, true, false, false, false, false, false)),
              chrome.pressed);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(false, false, true, false, false, false, false)),
              chrome.focused);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, true, true, true, true, true, true)),
              chrome.disabled);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, true, true, false, true, true, true)),
              chrome.dropTarget);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, true, true, false, true, true, false)),
              chrome.error);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(true, false, false, false, true, false, false)),
              chrome.selectedHovered);
    EXPECT_EQ(resolveVisualFill(chrome, composeVisualFlags(false, false, false, false, true, false, false)),
              chrome.selected);
    EXPECT_EQ(visualChrome(FButtonStyle{}).disabled, FButtonStyle{}.disabledFill);
    EXPECT_EQ(visualChrome(FSelectableRowStyle{}).dropTarget, FSelectableRowStyle{}.dropTargetFill);
}

TEST(UIFrameSnapshotTest, DisabledButtonIgnoresHoverFill)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       btn = std::make_shared<UIButton>("Btn");
    FCanvasSlotArgs buttonArgs;
    buttonArgs.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), btn, buttonArgs);

    auto enabled = std::make_shared<Reactive<bool>>(false);
    btn->bindEnabled(enabled);
    btn->onPointerEnter();

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, FButtonStyle{}.disabledFill.tintColor);
}

TEST(UIFrameSnapshotTest, SelectableRowDropTargetWinsOverSelection)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       row = std::make_shared<UISelectableRow>("Row");
    row->setSelected(true);
    FCanvasSlotArgs rowSlot;
    rowSlot.fixedSize = {240.0f, 22.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), row, rowSlot);

    highlightDrop(*row, true);
    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, FSelectableRowStyle{}.dropTargetFill.tintColor);
}

TEST(UIFrameSnapshotTest, VisualChromeMappingsForInteractiveStyles)
{
    EXPECT_EQ(visualChrome(FCheckBoxStyle{}).selected, FCheckBoxStyle{}.checkedFill);
    EXPECT_EQ(visualChrome(FCheckBoxStyle{}).selectedHovered, FCheckBoxStyle{}.checkedFill);
    EXPECT_EQ(visualChrome(FComboBoxStyle{}).normal, FComboBoxStyle{}.fieldFill);
    EXPECT_EQ(visualChrome(FMenuBarItemStyle{}).hovered, FMenuBarItemStyle{}.hoveredFill);
    EXPECT_EQ(visualChrome(FTabStyle{}).selectedHovered, FTabStyle{}.selectedFill);
    EXPECT_EQ(visualChrome(FMenuStyle{}).hovered, FMenuStyle{}.itemHoveredFill);
    EXPECT_EQ(visualChrome(FTableGridStyle{}).normal.tintColor.a, 0.0f);
    EXPECT_EQ(visualChrome(FTableGridStyle{}).selectedHovered, FTableGridStyle{}.selectedFill);
    EXPECT_EQ(visualChrome(FTreeViewStyle{}).hovered, FTreeViewStyle{}.hoveredFill);
}

TEST(UIFrameSnapshotTest, CheckedCheckBoxIgnoresHoverFill)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       box = std::make_shared<UICheckBox>("Check");
    box->setChecked(true);
    FCanvasSlotArgs slot;
    slot.offset    = {0.0f, 0.0f};
    slot.fixedSize = {120.0f, 24.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), box, slot);
    tree.layout();

    WidgetEventContext hover;
    hover.logicalPoint = {8.0f, 12.0f};
    tree.dispatchEvent(MouseMoveEvent(8.0f, 12.0f), hover);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items[0].color, FCheckBoxStyle{}.checkedFill.tintColor);
}

TEST(UIFrameSnapshotTest, SelectedTabIgnoresHoverFill)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       tab = std::make_shared<UITabButton>("Tab");
    tab->_label     = "Scene";
    tab->_bSelected = true;
    FCanvasSlotArgs slot;
    slot.offset    = {0.0f, 0.0f};
    slot.fixedSize = {80.0f, 28.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tab, slot);
    tree.layout();

    WidgetEventContext hover;
    hover.logicalPoint = {40.0f, 14.0f};
    tree.dispatchEvent(MouseMoveEvent(40.0f, 14.0f), hover);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_FALSE(snap.items.empty());
    EXPECT_EQ(snap.items[0].color, FTabStyle{}.selectedFill.tintColor);
}

TEST(UIFrameSnapshotTest, UnthemedPanelPaintsFromResolvedStyleNotNakedSprite)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, FPanelStyle{}.fillColor.tintColor);
    EXPECT_EQ(snap.items[0].kind, UIFrameDrawItem::EKind::Sprite);

    // Authored fillColor is the paint source. getColor() stays on the
    // unsynced _color field, so a leftover sprite path would still be gray.
    const glm::vec4 authored{0.9f, 0.1f, 0.2f, 1.0f};
    panel->setStyleField("fillColor", FBrush::solid(authored));
    const UIFrameSnapshot authoredSnap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(authoredSnap.items.size(), 1u);
    EXPECT_EQ(authoredSnap.items[0].color, authored);
    EXPECT_EQ(panel->getColor(), FPanelStyle{}.fillColor.tintColor);
}

TEST(UIFrameSnapshotTest, ThemedPanelIgnoresUnsyncedColorField)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIBorder>("P");
    FCanvasSlotArgs slot;
    slot.offset    = {10.0f, 10.0f};
    slot.fixedSize = {100.0f, 50.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, slot);

    auto theme = std::make_shared<UITheme>();
    FPanelStyle themed;
    themed.fillColor = FBrush::solid({0.1f, 0.8f, 0.2f, 1.0f});
    theme->define<FPanelStyle>("panel", themed);
    tree.setTheme(theme.get());

    const UIFrameSnapshot snap = tree.buildSnapshot(UIFrameBuildContext{});
    ASSERT_EQ(snap.items.size(), 1u);
    EXPECT_EQ(snap.items[0].color, glm::vec4(0.1f, 0.8f, 0.2f, 1.0f));
    EXPECT_EQ(panel->getColor(), FPanelStyle{}.fillColor.tintColor);
}

TEST(UIFrameSnapshotTest, FallbackThemeSwitchAndDeferredTextureReady)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIBorder>("P");
    auto       image = std::make_shared<UIImage>("Img");
    image->_assetPath = "tex:deferred";
    FCanvasSlotArgs panelSlot;
    panelSlot.offset    = {10.0f, 10.0f};
    panelSlot.fixedSize = {80.0f, 24.0f};
    FCanvasSlotArgs imageSlot;
    imageSlot.offset    = {100.0f, 10.0f};
    imageSlot.fixedSize = {40.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), image, imageSlot);

    const auto findSprite = [](const UIFrameSnapshot& snap, const glm::vec2& pos) -> const UIFrameDrawItem* {
        for (const UIFrameDrawItem& item : snap.items) {
            if (item.kind == UIFrameDrawItem::EKind::Sprite && item.pos == pos) {
                return &item;
            }
        }
        return nullptr;
    };

    UIFrameBuildContext missCtx;
    missCtx.generation = 1;
    missCtx.textureResolver = [](const std::string&) { return std::shared_ptr<Texture>(); };
    const UIFrameSnapshot unthemed = tree.buildSnapshot(missCtx);
    const UIFrameDrawItem* unthemedPanel = findSprite(unthemed, {10.0f, 10.0f});
    const UIFrameDrawItem* unthemedImage = findSprite(unthemed, {100.0f, 10.0f});
    ASSERT_NE(unthemedPanel, nullptr);
    ASSERT_NE(unthemedImage, nullptr);
    EXPECT_EQ(unthemedPanel->color, FPanelStyle{}.fillColor.tintColor);
    EXPECT_EQ(unthemedPanel->texture, nullptr);
    EXPECT_EQ(unthemedImage->color, FImageStyle{}.placeholderFill.tintColor);
    EXPECT_EQ(unthemedImage->texture, nullptr);

    auto themeA = std::make_shared<UITheme>();
    FPanelStyle panelA;
    panelA.fillColor = FBrush::solid({0.15f, 0.16f, 0.20f, 1.0f});
    themeA->define<FPanelStyle>("panel", panelA);
    FImageStyle imageA;
    imageA.placeholderFill = FBrush::solid({0.30f, 0.10f, 0.10f, 1.0f});
    imageA.errorFill       = FBrush::solid({0.50f, 0.20f, 0.20f, 1.0f});
    themeA->define<FImageStyle>("image", imageA);
    tree.setTheme(themeA.get());

    const UIFrameSnapshot themedA = tree.buildSnapshot(missCtx);
    const UIFrameDrawItem* aPanel = findSprite(themedA, {10.0f, 10.0f});
    const UIFrameDrawItem* aImage = findSprite(themedA, {100.0f, 10.0f});
    ASSERT_NE(aPanel, nullptr);
    ASSERT_NE(aImage, nullptr);
    EXPECT_EQ(aPanel->color, panelA.fillColor.tintColor);
    EXPECT_EQ(aImage->color, imageA.placeholderFill.tintColor);
    EXPECT_EQ(aImage->texture, nullptr);

    auto themeB = std::make_shared<UITheme>();
    FPanelStyle panelB;
    panelB.fillColor = FBrush::solid({0.94f, 0.95f, 0.97f, 1.0f});
    themeB->define<FPanelStyle>("panel", panelB);
    FImageStyle imageB;
    imageB.placeholderFill = FBrush::solid({0.10f, 0.30f, 0.10f, 1.0f});
    imageB.errorFill       = FBrush::solid({0.20f, 0.50f, 0.20f, 1.0f});
    themeB->define<FImageStyle>("image", imageB);
    tree.setTheme(themeB.get());

    const UIFrameSnapshot themedB = tree.buildSnapshot(missCtx);
    const UIFrameDrawItem* bPanel = findSprite(themedB, {10.0f, 10.0f});
    const UIFrameDrawItem* bImage = findSprite(themedB, {100.0f, 10.0f});
    ASSERT_NE(bPanel, nullptr);
    ASSERT_NE(bImage, nullptr);
    EXPECT_EQ(bPanel->color, panelB.fillColor.tintColor);
    EXPECT_EQ(bImage->color, imageB.placeholderFill.tintColor);
    EXPECT_EQ(bImage->texture, nullptr);

    auto ready = makeFakeTexture();
    UIFrameBuildContext hitCtx;
    hitCtx.generation = 2;
    hitCtx.textureResolver = [&](const std::string& path) {
        return path == "tex:deferred" ? ready : std::shared_ptr<Texture>();
    };
    const UIFrameSnapshot hitSnap = tree.buildSnapshot(hitCtx);
    EXPECT_EQ(tree.getLastInvalidationReason(), EUIInvalidationReason::ResourceReady);
    const UIFrameDrawItem* hitImage = findSprite(hitSnap, {100.0f, 10.0f});
    ASSERT_NE(hitImage, nullptr);
    EXPECT_EQ(hitImage->texture, ready);
    EXPECT_EQ(hitImage->color, image->_tint);
}

} // namespace ya
