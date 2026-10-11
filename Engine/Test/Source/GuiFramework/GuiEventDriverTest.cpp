// GUI input driver regression (shared app foundation). The driver is
// Foundation-only: it emits Core Events through IGuiEventSink, so it needs no
// WidgetTree / RHI to verify parse + execution semantics.

#include "App/Control/GuiEventDriver.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TabBar.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

struct RecordingSink final : public IGuiEventSink
{
    std::vector<EEvent::T> types;
    int                    frames = 0;
    std::string            checkpoint;

    void dispatch(const Event& event, const glm::vec2&) override
    {
        types.push_back(event.getEventType());
    }
};

struct TreeSink final : public IGuiEventSink
{
    WidgetTree& tree;

    explicit TreeSink(WidgetTree& inTree) : tree(inTree) {}

    void dispatch(const Event& event, const glm::vec2& logicalPoint) override
    {
        WidgetEventContext ctx;
        ctx.logicalPoint = logicalPoint;
       (void)tree.dispatchEvent(event, ctx);
    }
};

} // namespace

TEST(GuiEventDriverTest, ParsesScenarioSteps)
{
    const std::string jsonl = R"json(
{"frame":2}
{"event":"mouse_move","x":10,"y":20}
{"event":"mouse_press","button":0,"x":11,"y":21}
{"event":"key_press","key":"Enter"}
{"drag":{"from":[0,0],"to":[30,30],"steps":4}}
{"checkpoint":"after"}
{"assert":{"widget":"Example","control":{"checked":true}}}
)json";

    std::string error;
    const auto  steps = parseGuiScenario(jsonl, &error);
    ASSERT_TRUE(error.empty());
    ASSERT_EQ(steps.size(), 7u);
    EXPECT_EQ(steps[0].kind, EGuiScenarioStepKind::Frame);
    EXPECT_EQ(steps[0].frame, 2u);
    EXPECT_EQ(steps[1].kind, EGuiScenarioStepKind::MouseMove);
    EXPECT_EQ(steps[2].kind, EGuiScenarioStepKind::MousePress);
    EXPECT_EQ(steps[3].kind, EGuiScenarioStepKind::KeyPress);
    EXPECT_EQ(steps[3].key, EKey::Enter);
    EXPECT_EQ(steps[3].keyMod, 0u);
    EXPECT_EQ(steps[4].kind, EGuiScenarioStepKind::Drag);
    EXPECT_EQ(steps[4].dragSteps, 4);
    EXPECT_EQ(steps[5].kind, EGuiScenarioStepKind::Checkpoint);
    EXPECT_EQ(steps[5].tag, "after");
    EXPECT_EQ(steps[6].kind, EGuiScenarioStepKind::Assert);
    EXPECT_EQ(steps[6].assertion, R"json({"control":{"checked":true},"widget":"Example"})json");
}

TEST(GuiEventDriverTest, ParsesKeyPressModifiers)
{
    const std::string jsonl = R"json(
{"event":"key_press","key":"Left","shift":true}
{"event":"key_press","key":"A","primary":true}
)json";
    std::string error;
    const auto  steps = parseGuiScenario(jsonl, &error);
    ASSERT_TRUE(error.empty());
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].key, EKey::Left);
    EXPECT_NE(steps[0].keyMod & EKeyMod::Shift, 0u);
    EXPECT_EQ(steps[1].key, EKey::K_A);
#if defined(__APPLE__)
    EXPECT_NE(steps[1].keyMod & EKeyMod::LMeta, 0u);
#else
    EXPECT_NE(steps[1].keyMod & EKeyMod::LCtrl, 0u);
#endif
}

TEST(GuiEventDriverTest, ExecutesDragAndKeyThroughSink)
{
    RecordingSink sink;
    GuiScenarioExecutor executor(
        sink,
        [&](uint32_t count) { sink.frames += static_cast<int>(count); },
        [&](const std::string& tag) { sink.checkpoint = tag; });

    const std::string jsonl = R"json(
{"frame":1}
{"drag":{"from":[0,0],"to":[20,20],"steps":3}}
{"event":"key_press","key":"Space"}
{"checkpoint":"done"}
)json";

    ASSERT_TRUE(executor.runJsonl(jsonl));
    EXPECT_EQ(sink.frames, 1);
    EXPECT_EQ(sink.checkpoint, "done");

    // Drag follows the same pointer contract as press/release: bootstrap the
    // cursor with a move, then press + 3 interpolated moves + release.
    ASSERT_EQ(sink.types.size(), 7u);
    EXPECT_EQ(sink.types[0], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[1], EEvent::MouseButtonPressed);
    EXPECT_EQ(sink.types[2], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[3], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[4], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[5], EEvent::MouseButtonReleased);
    EXPECT_EQ(sink.types[6], EEvent::KeyPressed);
}

TEST(GuiEventDriverTest, AssertionCallbackStopsExecutorOnFailure)
{
    RecordingSink sink;
    std::string   assertion;
    GuiScenarioExecutor executor(
        sink,
        [](uint32_t) {},
        {},
        [&](std::string_view value) {
            assertion = value;
            return false;
        });

    ASSERT_FALSE(executor.runJsonl(R"json(
{"assert":{"lastRoute":{"target":"Missing"}}}
{"event":"key_press","key":"Enter"}
)json"));
    EXPECT_EQ(assertion, R"json({"lastRoute":{"target":"Missing"}})json");
    EXPECT_TRUE(sink.types.empty());
}

TEST(GuiEventDriverTest, ScenarioDrivesWidgetTreeAndDumpAssertsHoverContract)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       a = std::make_shared<UIButton>("A");
    auto b = std::make_shared<UIButton>("B");
    FCanvasSlotArgs aSlot;
    aSlot.offset = {10.0f, 10.0f};
    aSlot.fixedSize = {60.0f, 24.0f};
    FCanvasSlotArgs bSlot;
    bSlot.offset = {120.0f, 10.0f};
    bSlot.fixedSize = {60.0f, 24.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), a, aSlot);
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), b, bSlot);
    tree.layout();

    TreeSink sink{tree};
    GuiScenarioExecutor executor(sink, [](uint32_t) {}, {});
    const std::string jsonl = R"json(
{"event":"mouse_move","x":30,"y":20}
{"event":"mouse_press","button":0,"x":30,"y":20}
{"event":"mouse_release","button":0,"x":30,"y":20}
{"event":"mouse_press","button":0,"x":150,"y":20}
{"event":"mouse_release","button":0,"x":150,"y":20}
)json";
    ASSERT_TRUE(executor.runJsonl(jsonl));

    const auto dump = dumpWidgetTree(tree);
    const auto* aNode = findWidgetNode(dump, "A");
    const auto* bNode = findWidgetNode(dump, "B");
    ASSERT_NE(aNode, nullptr);
    ASSERT_NE(bNode, nullptr);

    // Pressing B retires the stale hover on A and arms B's own hover/focus.
    EXPECT_FALSE((*aNode)["hovered"]);
    EXPECT_FALSE((*aNode)["focused"]);
    EXPECT_TRUE((*bNode)["hovered"]);
    EXPECT_TRUE((*bNode)["focused"]);
}

TEST(GuiEventDriverTest, ParsesWidgetTargetedSteps)
{
    const std::string jsonl = R"json(
{"click":{"widget":"DialogOK","button":0,"at":"center"}}
{"click":{"widget":"Tab_Console","part":"close"}}
{"click":{"widget":"Tab_Hierarchy","at":[0.0,0.5]}}
{"wheel":{"widget":"DemoScroll","dy":-4}}
{"drag":{"widget":"Tab_Assets","to_widget":"Tab_Hierarchy","to_at":[0.0,0.5],"steps":12}}
{"drag":{"widget":"DemoSplit","part":"handle","by":[100,0],"steps":8}}
{"event":"mouse_move","widget":"TooltipBtn"}
)json";

    std::string error;
    const auto  steps = parseGuiScenario(jsonl, &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(steps.size(), 7u);

    EXPECT_EQ(steps[0].kind, EGuiScenarioStepKind::WidgetClick);
    EXPECT_EQ(steps[0].widget, "DialogOK");
    EXPECT_TRUE(steps[0].widgetPart.empty());
    EXPECT_EQ(steps[0].anchor, (glm::vec2{0.5f, 0.5f}));
    EXPECT_EQ(steps[0].button, 0);

    EXPECT_EQ(steps[1].widgetPart, "close");
    EXPECT_EQ(steps[2].anchor, (glm::vec2{0.0f, 0.5f}));

    EXPECT_EQ(steps[3].kind, EGuiScenarioStepKind::MouseWheel);
    EXPECT_EQ(steps[3].widget, "DemoScroll");
    EXPECT_EQ(steps[3].wheel, (glm::vec2{0.0f, -4.0f}));

    EXPECT_EQ(steps[4].kind, EGuiScenarioStepKind::Drag);
    EXPECT_EQ(steps[4].widget, "Tab_Assets");
    EXPECT_EQ(steps[4].dragToWidget, "Tab_Hierarchy");
    EXPECT_EQ(steps[4].dragToAnchor, (glm::vec2{0.0f, 0.5f}));
    EXPECT_EQ(steps[4].dragSteps, 12);

    EXPECT_EQ(steps[5].widgetPart, "handle");
    EXPECT_EQ(steps[5].dragBy, (glm::vec2{100.0f, 0.0f}));
    EXPECT_TRUE(steps[5].dragToWidget.empty());

    EXPECT_EQ(steps[6].kind, EGuiScenarioStepKind::MouseMove);
    EXPECT_EQ(steps[6].widget, "TooltipBtn");
}

TEST(GuiEventDriverTest, ParsesRejectsInvalidWidgetSteps)
{
    struct Case
    {
        const char* line;
        const char* fragment;
    };
    const Case cases[] = {
        {"{\"click\":{\"button\":0}}", "widget"},
        {"{\"wheel\":{}}", "widget"},
        {"{\"drag\":{\"widget\":\"X\"}}", "by"},
        {"{\"click\":{\"widget\":\"X\",\"at\":[2.0,0.5]}}", "normalized"},
        {"{\"drag\":{\"widget\":\"X\",\"to_widget\":\"\"}}", "to_widget"},
    };
    for (const Case& testCase : cases) {
        std::string error;
        const auto  steps = parseGuiScenario(testCase.line, &error);
        EXPECT_TRUE(steps.empty()) << testCase.line;
        EXPECT_FALSE(error.empty()) << testCase.line;
        EXPECT_NE(error.find(testCase.fragment), std::string::npos)
            << testCase.line << " -> " << error;
    }
}

TEST(GuiEventDriverTest, WidgetClickEmitsPressAndReleaseAtResolvedPoint)
{
    RecordingSink sink;
    GuiScenarioExecutor executor(
        sink,
        [](uint32_t) {},
        {},
        {},
        [](const GuiScenarioStep& step, glm::vec2& outPoint, glm::vec2&, std::string&) {
            outPoint = glm::vec2{40.0f, 12.0f} + glm::vec2{30.0f, 12.0f} * step.anchor;
            return true;
        });

    ASSERT_TRUE(executor.runJsonl(R"json(
{"click":{"widget":"A"}}
)json"));
    // Bootstrap move + press + release at the resolved center.
    ASSERT_EQ(sink.types.size(), 3u);
    EXPECT_EQ(sink.types[0], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[1], EEvent::MouseButtonPressed);
    EXPECT_EQ(sink.types[2], EEvent::MouseButtonReleased);
}

TEST(GuiEventDriverTest, WidgetStepsFailWithoutResolverOrOnResolverFailure)
{
    RecordingSink sink;

    GuiScenarioExecutor withoutResolver(sink, [](uint32_t) {});
    EXPECT_FALSE(withoutResolver.runJsonl(R"json(
{"click":{"widget":"Missing"}}
{"event":"key_press","key":"Enter"}
)json"));
    EXPECT_TRUE(sink.types.empty());

    GuiScenarioExecutor failingResolver(
        sink,
        [](uint32_t) {},
        {},
        {},
        [](const GuiScenarioStep&, glm::vec2&, glm::vec2&, std::string& error) {
            error = "widget 'X' not found";
            return false;
        });
    EXPECT_FALSE(failingResolver.runJsonl(R"json(
{"drag":{"widget":"X","by":[10,0]}}
)json"));
    EXPECT_TRUE(sink.types.empty());
}

TEST(GuiEventDriverTest, ResolveWidgetEventTargetAnchorsPartsAndFailsLoudly)
{
    WidgetTree tree({.width = 800, .height = 600});

    auto       a = std::make_shared<UIButton>("A");
    FCanvasSlotArgs aSlot;
    aSlot.offset    = {10.0f, 10.0f};
    aSlot.fixedSize = {60.0f, 24.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), a, aSlot);

    auto       covered = std::make_shared<UIButton>("Covered");
    FCanvasSlotArgs coveredSlot;
    coveredSlot.offset    = {80.0f, 10.0f};
    coveredSlot.fixedSize = {60.0f, 24.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), covered, coveredSlot);
    auto       over = std::make_shared<UIButton>("Over");
    FCanvasSlotArgs overSlot;
    overSlot.offset    = {80.0f, 10.0f};
    overSlot.fixedSize = {60.0f, 24.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), over, overSlot);

    auto       hidden = std::make_shared<UIButton>("Hidden");
    FCanvasSlotArgs hiddenSlot;
    hiddenSlot.offset    = {200.0f, 10.0f};
    hiddenSlot.fixedSize = {60.0f, 24.0f};
    hidden->setVisibility(EWidgetVisibility::Hidden);
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), hidden, hiddenSlot);

    auto split = std::make_shared<UISplitPane>("Split");
    FCanvasSlotArgs splitSlot;
    splitSlot.offset    = {10.0f, 100.0f};
    splitSlot.fixedSize = {200.0f, 100.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split, splitSlot);
    auto paneA = std::make_shared<UIButton>("PaneA");
    auto paneB = std::make_shared<UIButton>("PaneB");
   (void)tree.attach(*split, paneA);
   (void)tree.attach(*split, paneB);

    auto tab = std::make_shared<UITabButton>("Tab_X");
    tab->_bClosable = true;
    FCanvasSlotArgs tabSlot;
    tabSlot.offset    = {300.0f, 100.0f};
    tabSlot.fixedSize = {90.0f, 26.0f};
   (void)tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), tab, tabSlot);

    tree.layout();

    glm::vec2 point;
    std::string error;

    // Center anchor lands at the middle of the laid-out rect.
    ASSERT_TRUE(resolveWidgetEventTarget(tree, "A", "", {0.5f, 0.5f}, point, error)) << error;
    EXPECT_FLOAT_EQ(point.x, 40.0f);
    EXPECT_FLOAT_EQ(point.y, 22.0f);

    // Normalized anchors reach the exact rect edges.
    ASSERT_TRUE(resolveWidgetEventTarget(tree, "A", "", {0.0f, 0.5f}, point, error)) << error;
    EXPECT_FLOAT_EQ(point.x, 10.0f);
    EXPECT_FLOAT_EQ(point.y, 22.0f);

    // Unknown widget fails with the name in the error.
    EXPECT_FALSE(resolveWidgetEventTarget(tree, "Missing", "", {0.5f, 0.5f}, point, error));
    EXPECT_NE(error.find("'Missing'"), std::string::npos);

    // A widget covered by a later sibling fails instead of clicking the top.
    EXPECT_FALSE(resolveWidgetEventTarget(tree, "Covered", "", {0.5f, 0.5f}, point, error));
    EXPECT_NE(error.find("'Over'"), std::string::npos);

    // A hidden widget is not clickable.
    EXPECT_FALSE(resolveWidgetEventTarget(tree, "Hidden", "", {0.5f, 0.5f}, point, error));

    // part:"handle" resolves to the divider rect of the split.
    const Rect2D divider = split->getDividerRect();
    ASSERT_GT(divider.extent.x * divider.extent.y, 0.0f);
    ASSERT_TRUE(resolveWidgetEventTarget(tree, "Split", "handle", {0.5f, 0.5f}, point, error)) << error;
    EXPECT_NEAR(point.x, divider.pos.x + divider.extent.x * 0.5f, 0.01f);
    EXPECT_NEAR(point.y, divider.pos.y + divider.extent.y * 0.5f, 0.01f);
    EXPECT_FALSE(resolveWidgetEventTarget(tree, "A", "handle", {0.5f, 0.5f}, point, error));

    // part:"close" resolves to the tab's close hit-zone center.
    const Rect2D closeZone = tab->closeHitRect();
    ASSERT_TRUE(resolveWidgetEventTarget(tree, "Tab_X", "close", {0.5f, 0.5f}, point, error)) << error;
    EXPECT_NEAR(point.x, closeZone.pos.x + closeZone.extent.x * 0.5f, 0.01f);
    EXPECT_NEAR(point.y, closeZone.pos.y + closeZone.extent.y * 0.5f, 0.01f);

    // Unknown parts fail with the part named.
    EXPECT_FALSE(resolveWidgetEventTarget(tree, "A", "nope", {0.5f, 0.5f}, point, error));
    EXPECT_NE(error.find("'nope'"), std::string::npos);
}

} // namespace ya
