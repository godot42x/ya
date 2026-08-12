// GUI input driver regression (shared app foundation). The driver is
// Foundation-only: it emits Core Events through IGuiEventSink, so it needs no
// WidgetTree / RHI to verify parse + execution semantics.

#include "Core/Application/GuiEventDriver.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Widgets/Controls/Button.h"

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
        tree.dispatchEvent(event, ctx);
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
)json";

    std::string error;
    const auto  steps = parseGuiScenario(jsonl, &error);
    ASSERT_TRUE(error.empty());
    ASSERT_EQ(steps.size(), 6u);
    EXPECT_EQ(steps[0].kind, EGuiScenarioStepKind::Frame);
    EXPECT_EQ(steps[0].frame, 2u);
    EXPECT_EQ(steps[1].kind, EGuiScenarioStepKind::MouseMove);
    EXPECT_EQ(steps[2].kind, EGuiScenarioStepKind::MousePress);
    EXPECT_EQ(steps[3].kind, EGuiScenarioStepKind::KeyPress);
    EXPECT_EQ(steps[3].key, EKey::Enter);
    EXPECT_EQ(steps[4].kind, EGuiScenarioStepKind::Drag);
    EXPECT_EQ(steps[4].dragSteps, 4);
    EXPECT_EQ(steps[5].kind, EGuiScenarioStepKind::Checkpoint);
    EXPECT_EQ(steps[5].tag, "after");
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

    // drag = press + 3 moves + release.
    ASSERT_EQ(sink.types.size(), 6u);
    EXPECT_EQ(sink.types[0], EEvent::MouseButtonPressed);
    EXPECT_EQ(sink.types[1], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[2], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[3], EEvent::MouseMoved);
    EXPECT_EQ(sink.types[4], EEvent::MouseButtonReleased);
    EXPECT_EQ(sink.types[5], EEvent::KeyPressed);
}

TEST(GuiEventDriverTest, ScenarioDrivesWidgetTreeAndDumpAssertsHoverContract)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       a = std::make_shared<UIButton>("A");
    a->_position  = {10.0f, 10.0f};
    a->_size      = {60.0f, 24.0f};
    auto b = std::make_shared<UIButton>("B");
    b->_position  = {120.0f, 10.0f};
    b->_size      = {60.0f, 24.0f};
    tree.attachToLayer(WidgetTree::ELayer::Content, a);
    tree.attachToLayer(WidgetTree::ELayer::Content, b);
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

} // namespace ya
