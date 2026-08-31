// Phase 1 regression guards for the Game UI WidgetTree (ui-widget-tree-refactor):
// single-parent contract, detached lifecycle, zOrder/layer hit order, focus /
// capture, tree teardown, and the UITypeRegistry module live-instance guard.
// The target links ONLY the GUI closure, proving ya-gui-widgets has no
// Scene/ECS/Render3D/Host dependency.

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Declarative/Declarative.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/WidgetTreeDump.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Dialog.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockFloatingWindow.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UIBehavior.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <vector>

namespace ya
{

namespace
{

WidgetEventContext pointAt(float x, float y)
{
    WidgetEventContext ctx;
    ctx.logicalPoint = {x, y};
    return ctx;
}

template <typename T>
T* findDescendantOfType(UIElement& root)
{
    if (auto* typed = dynamic_cast<T*>(&root)) {
        return typed;
    }
    for (const UIElementRef& child : root.getChildren()) {
        if (!child) {
            continue;
        }
        if (auto* typed = findDescendantOfType<T>(*child)) {
            return typed;
        }
    }
    return nullptr;
}

std::shared_ptr<UIButton> makeButton(const std::string& name, glm::vec2 pos, glm::vec2 size)
{
    (void)pos;
    (void)size;
    return std::make_shared<UIButton>(name);
}

FCanvasSlotArgs makeButtonSlot(glm::vec2 pos, glm::vec2 size)
{
    FCanvasSlotArgs args;
    args.offset = pos;
    args.fixedSize = size;
    return args;
}

/// Test-only widget that consumes keyboard events and records them.
struct TestKeyWidget : public UIElement
{
    explicit TestKeyWidget(std::string name) : UIElement(std::move(name)) {}

    int keyHits = 0;

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        (void)ctx;
        if (event.getEventType() == EEvent::KeyPressed) {
            ++keyHits;
            return true;
        }
        return false;
    }
};

struct TestRouteWidget final : public UIElement
{
    explicit TestRouteWidget(std::string name, std::vector<std::string>& deliveries)
        : UIElement(std::move(name))
        , _deliveries(deliveries)
    {
    }

    bool bHandleTarget = false;
    bool bHandleBubble = false;

    bool previewInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        (void)event;
        (void)ctx;
        _deliveries.push_back(_name + ".preview");
        return false;
    }

    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        (void)event;
        (void)ctx;
        _deliveries.push_back(_name + ".target");
        return bHandleTarget;
    }

    bool bubbleInputEvent(const Event& event, const WidgetEventContext& ctx) override
    {
        (void)event;
        (void)ctx;
        _deliveries.push_back(_name + ".bubble");
        return bHandleBubble;
    }

  private:
    std::vector<std::string>& _deliveries;
};

struct TestDropTarget final : public UIElement
{
    explicit TestDropTarget(std::string name) : UIElement(std::move(name)) {}

    bool bAccept = true;
    int highlightChanges = 0;
    int drops = 0;
    std::string lastPayload;

    bool canAcceptDrop(const std::string&, const glm::vec2& point) override
    {
        return bAccept && hitTestLayoutRect(point);
    }

    void onDrop(const std::string& payload, const glm::vec2&) override
    {
        ++drops;
        lastPayload = payload;
    }

    void setDropHighlight(bool bHighlight) override
    {
        (void)bHighlight;
        ++highlightChanges;
    }
};

struct OperationDropTarget final : public UIElement
{
    explicit OperationDropTarget(std::string name) : UIElement(std::move(name)) {}
    bool accepted = false;
    std::string receivedType;
    bool canAcceptDrop(const UIDragDropOperation& operation, const glm::vec2&) override
    {
        receivedType = operation.typeId;
        return operation.typeId == "test.asset";
    }
    void onDrop(const UIDragDropOperation& operation, const glm::vec2&) override
    {
        accepted = operation.payload == "asset:42";
    }
};

struct DragDetectWidget final : public UIElement
{
    explicit DragDetectWidget(std::string name) : UIElement(std::move(name)) {}
    UIDragDropOperationRef onDragDetected(const FDragDetectedEvent& event) override
    {
        detected = event.currentPoint.x > event.startPoint.x;
        auto op = std::make_shared<UIDragDropOperation>();
        op->typeId = "test.detected";
        op->payload = "detected";
        op->ghostLabel = "Detected";
        return op;
    }
    bool detected = false;
};

struct TestBehavior final : public UIBehavior
{
    int attached = 0;
    int detached = 0;
    int previewHits = 0;
    int targetHits = 0;
    int bubbleHits = 0;
    int tickHits = 0;
    bool bHandlePreview = false;
    bool bHandleTarget = false;
    bool bHandleBubble = false;
    bool bTick = false;

    void onAttached(UIElement& owner) override
    {
        UIBehavior::onAttached(owner);
        ++attached;
    }

    void onDetached(UIElement& owner) override
    {
        ++detached;
        UIBehavior::onDetached(owner);
    }

    [[nodiscard]] bool wantsTick() const override { return bTick; }

    void tick(UIElement& owner, float deltaSeconds) override
    {
        (void)owner;
        (void)deltaSeconds;
        ++tickHits;
    }

    bool previewInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx) override
    {
        (void)owner;
        (void)event;
        (void)ctx;
        ++previewHits;
        return bHandlePreview;
    }

    bool handleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx) override
    {
        (void)owner;
        (void)event;
        (void)ctx;
        ++targetHits;
        return bHandleTarget;
    }

    bool bubbleInputEvent(UIElement& owner, const Event& event, const WidgetEventContext& ctx) override
    {
        (void)owner;
        (void)event;
        (void)ctx;
        ++bubbleHits;
        return bHandleBubble;
    }

    void requestPaint() { invalidateOwnerPaint(); }
};

struct TestDragBehavior final : public UIBehavior
{
    bool bSource = false;
    bool bAccept = false;
    bool detected = false;
    bool dropped = false;
    int highlightChanges = 0;
    std::string payload;

    UIDragDropOperationRef onDragDetected(UIElement& owner, const FDragDetectedEvent& event) override
    {
        (void)owner;
        detected = event.currentPoint.x >= event.startPoint.x;
        if (!bSource) {
            return nullptr;
        }
        auto op = std::make_shared<UIDragDropOperation>();
        op->typeId = "behavior.payload";
        op->payload = payload.empty() ? "behavior.payload.1" : payload;
        op->ghostLabel = "Behavior";
        return op;
    }

    bool canAcceptDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override
    {
        (void)owner; (void)logicalPoint;
        return bAccept && operation.typeId == "behavior.payload";
    }

    void onDrop(UIElement& owner, const UIDragDropOperation& operation, const glm::vec2& logicalPoint) override
    {
        (void)owner; (void)logicalPoint;
        dropped = operation.payload == (payload.empty() ? "behavior.payload.1" : payload);
    }

    void setDropHighlight(UIElement& owner, bool bHighlight) override
    {
        (void)owner; (void)bHighlight;
        ++highlightChanges;
    }
};

} // namespace

// === WidgetTreeDump ===

TEST(WidgetTreeTest, DumpTreeCapturesRectAndTransientState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 80.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({100.0f, 80.0f}, {80.0f, 32.0f}));
    tree.layout();

    const nlohmann::json dump = dumpWidgetTree(tree);
    const nlohmann::json* node = findWidgetNode(dump, "B");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ((*node)["rect"]["x"], 100.0f);
    EXPECT_EQ((*node)["rect"]["y"], 80.0f);
    EXPECT_EQ((*node)["rect"]["w"], 80.0f);
    EXPECT_EQ((*node)["rect"]["h"], 32.0f);
    EXPECT_FALSE((*node)["hovered"]);
    EXPECT_FALSE((*node)["focused"]);

    // Hover then press: the dump reflects live transient state from the tree.
    tree.dispatchEvent(MouseMoveEvent(120.0f, 96.0f), pointAt(120.0f, 96.0f));
    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 96.0f));
    const nlohmann::json pressed = dumpWidgetTree(tree);
    const nlohmann::json* pressedNode = findWidgetNode(pressed, "B");
    ASSERT_NE(pressedNode, nullptr);
    EXPECT_TRUE((*pressedNode)["hovered"]);
    EXPECT_TRUE((*pressedNode)["focused"]);
    EXPECT_TRUE((*pressedNode)["captured"]);
}

TEST(WidgetTreeTest, DumpUsesPerControlRuntimeDiagnosticsHooks)
{
    WidgetTree tree({.width = 800, .height = 600});

    auto checkBox = std::make_shared<UICheckBox>("Check");
    checkBox->_bChecked = true;
    auto slider = std::make_shared<UISlider>("Slider");
    slider->_value = 0.75f;
    slider->_step = 0.1f;
    auto combo = std::make_shared<UIComboBox>("Combo");
    combo->_items = {"One", "Two"};
    combo->_selectedIndex = 1;
    auto scroll = std::make_shared<UIScrollViewport>("Scroll");
    auto split = std::make_shared<UISplitPane>("Split");
    split->setSplitRatio(0.35f);

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), checkBox);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), slider);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), combo);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), scroll);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), split);
    tree.layout();

    const nlohmann::json dump = dumpWidgetTree(tree);

    const nlohmann::json* checkNode = findWidgetNode(dump, "Check");
    ASSERT_NE(checkNode, nullptr);
    EXPECT_EQ((*checkNode)["control"]["type"], "checkBox");
    EXPECT_TRUE((*checkNode)["control"]["checked"]);

    const nlohmann::json* sliderNode = findWidgetNode(dump, "Slider");
    ASSERT_NE(sliderNode, nullptr);
    EXPECT_EQ((*sliderNode)["control"]["type"], "slider");
    EXPECT_FLOAT_EQ((*sliderNode)["control"]["value"], 0.75f);
    EXPECT_FLOAT_EQ((*sliderNode)["control"]["step"], 0.1f);

    const nlohmann::json* comboNode = findWidgetNode(dump, "Combo");
    ASSERT_NE(comboNode, nullptr);
    EXPECT_EQ((*comboNode)["control"]["type"], "comboBox");
    EXPECT_EQ((*comboNode)["control"]["selectedIndex"], 1);
    EXPECT_EQ((*comboNode)["control"]["label"], "Two");

    const nlohmann::json* scrollNode = findWidgetNode(dump, "Scroll");
    ASSERT_NE(scrollNode, nullptr);
    EXPECT_EQ((*scrollNode)["control"]["type"], "scrollViewport");
    EXPECT_FLOAT_EQ((*scrollNode)["control"]["offset"], 0.0f);

    const nlohmann::json* splitNode = findWidgetNode(dump, "Split");
    ASSERT_NE(splitNode, nullptr);
    EXPECT_EQ((*splitNode)["control"]["type"], "splitPane");
    EXPECT_FLOAT_EQ((*splitNode)["control"]["ratio"], 0.35f);
    EXPECT_TRUE((*splitNode)["control"].contains("divider"));
}

TEST(WidgetTreeTest, DragOperationReachesTypedDropTarget)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto target = std::make_shared<OperationDropTarget>("Target");
    FCanvasSlotArgs targetSlot; targetSlot.offset = {40.0f, 40.0f}; targetSlot.fixedSize = {160.0f, 100.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    auto operation = std::make_shared<UIDragDropOperation>();
    operation->typeId = "test.asset";
    operation->payload = "asset:42";
    operation->ghostLabel = "Asset";
    tree.beginDrag(nullptr, operation, {}, false);
    tree.updateDrag({80.0f, 80.0f});
    EXPECT_EQ(target->receivedType, "test.asset");
    tree.endDrag({80.0f, 80.0f});
    EXPECT_TRUE(target->accepted);
    EXPECT_FALSE(tree.isDragging());
}

TEST(WidgetTreeTest, DragDetectionInvokesWidgetCallbackWithoutDragSourceControl)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto source = std::make_shared<DragDetectWidget>("Source");
    FCanvasSlotArgs sourceSlot; sourceSlot.offset = {20.0f, 20.0f}; sourceSlot.fixedSize = {120.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.layout();

    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f));
    tree.dispatchEvent(MouseMoveEvent(60.0f, 40.0f), pointAt(60.0f, 40.0f));

    EXPECT_TRUE(source->detected);
    ASSERT_TRUE(tree.isDragging());
    ASSERT_NE(tree.getDragOperation(), nullptr);
    EXPECT_EQ(tree.getDragOperation()->typeId, "test.detected");
    tree.cancelDrag();
}

TEST(WidgetTreeTest, DragGhostLabelUsesCanvasSlotInsteadOfChildZeroSize)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       source = std::make_shared<UIPanel>("Source");
    FCanvasSlotArgs sourceSlot; sourceSlot.offset = {20.0f, 20.0f}; sourceSlot.fixedSize = {120.0f, 80.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot).valid());

    auto operation = std::make_shared<UIDragDropOperation>();
    operation->typeId = "test.asset";
    operation->payload = "asset:42";
    operation->ghostLabel = "Ghost";
    tree.beginDrag(source.get(), operation, {}, true);
    (void)tree.buildSnapshot(UIFrameBuildContext{});

    const nlohmann::json dump = dumpWidgetTree(tree);
    const auto* labelNode = findWidgetNode(dump, "DragGhostLabel");
    ASSERT_NE(labelNode, nullptr);
    EXPECT_EQ((*labelNode)["slot"]["type"], "canvas");
    EXPECT_GT((*labelNode)["rect"]["w"].get<float>(), 0.0f);
    EXPECT_GT((*labelNode)["rect"]["h"].get<float>(), 0.0f);

    tree.cancelDrag();
}

TEST(WidgetTreeTest, BehaviorLifecycleTickAndInvalidationFollowOwner)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto panel = std::make_shared<UIPanel>("BehaviorHost");
    FCanvasSlotArgs panelSlot; panelSlot.fixedSize = {120.0f, 60.0f};
    auto behavior = std::make_shared<TestBehavior>();
    behavior->bTick = true;
    panel->addBehavior(behavior);

    EXPECT_EQ(behavior->attached, 0);
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());
    EXPECT_EQ(behavior->attached, 1);
    EXPECT_TRUE(panel->hasBehavior(*behavior));

    (void)tree.buildSnapshot({});
    const GuiPerfStats before = tree.getPerfStats();
    behavior->requestPaint();
    (void)tree.buildSnapshot({});
    EXPECT_GT(tree.getPerfStats().paintDirtyTransitions, before.paintDirtyTransitions);

    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(behavior->tickHits, 1);

    tree.detach(*panel);
    EXPECT_EQ(behavior->detached, 1);
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(behavior->tickHits, 1);
}

TEST(WidgetTreeTest, BehaviorParticipatesInPreviewTargetAndBubbleRouting)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto root = std::make_shared<UIPanel>("Root");
    FCanvasSlotArgs rootSlot; rootSlot.offset = {20.0f, 20.0f}; rootSlot.fixedSize = {200.0f, 160.0f};
    root->_hitFilter = EWidgetHitFilter::Stop;
    auto child = std::make_shared<UIPanel>("Child");
    child->_hitFilter = EWidgetHitFilter::Pass;
    root->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            FCanvasSlotArgs args;
            args.offset    = {10.0f, 10.0f};
            args.fixedSize = {80.0f, 40.0f};
            canvas->apply(args);
        }
    });

    auto rootBehavior = std::make_shared<TestBehavior>();
    auto childBehavior = std::make_shared<TestBehavior>();
    root->addBehavior(rootBehavior);
    child->addBehavior(childBehavior);

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root, rootSlot).valid());
    tree.layout();

    rootBehavior->bHandleBubble = true;
    childBehavior->bHandleTarget = true;
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_GE(rootBehavior->previewHits, 1);
    EXPECT_GE(childBehavior->targetHits, 1);
    EXPECT_GE(rootBehavior->bubbleHits, 1);
}

TEST(WidgetTreeTest, BehaviorCanActAsDragSourceAndDropTargetWithoutDedicatedWidgetSubclass)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto source = std::make_shared<UIPanel>("BehaviorSource");
    FCanvasSlotArgs sourceSlot; sourceSlot.offset = {20.0f, 20.0f}; sourceSlot.fixedSize = {120.0f, 60.0f};
    source->_hitFilter = EWidgetHitFilter::Stop;
    auto target = std::make_shared<UIPanel>("BehaviorTarget");
    FCanvasSlotArgs targetSlot; targetSlot.offset = {220.0f, 20.0f}; targetSlot.fixedSize = {120.0f, 60.0f};
    target->_hitFilter = EWidgetHitFilter::Stop;

    auto sourceBehavior = std::make_shared<TestDragBehavior>();
    sourceBehavior->bSource = true;
    auto targetBehavior = std::make_shared<TestDragBehavior>();
    targetBehavior->bAccept = true;
    targetBehavior->payload = "behavior.payload.1";
    source->addBehavior(sourceBehavior);
    target->addBehavior(targetBehavior);

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot).valid());
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot).valid());
    tree.layout();

    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(40.0f, 40.0f));
    tree.dispatchEvent(MouseMoveEvent(80.0f, 40.0f), pointAt(80.0f, 40.0f));
    EXPECT_TRUE(sourceBehavior->detected);
    ASSERT_TRUE(tree.isDragging());
    tree.dispatchEvent(MouseMoveEvent(260.0f, 40.0f), pointAt(260.0f, 40.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(260.0f, 40.0f));

    EXPECT_TRUE(targetBehavior->dropped);
    EXPECT_GT(targetBehavior->highlightChanges, 0);
    EXPECT_FALSE(tree.isDragging());
}

TEST(WidgetTreeTest, RouteStateTracksPointerCaptureAndFocusPaths)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto panel = std::make_shared<UIPanel>("Panel");
    FCanvasSlotArgs panelSlot; panelSlot.offset = {100.0f, 80.0f}; panelSlot.fixedSize = {160.0f, 80.0f};
    auto button = makeButton("Button", {20.0f, 10.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);
    panel->addDetachedChild(button, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            FCanvasSlotArgs args;
            args.offset    = {20.0f, 10.0f};
            args.fixedSize = {80.0f, 32.0f};
            canvas->apply(args);
        }
    });
    tree.layout();

    tree.dispatchEvent(MouseMoveEvent(130.0f, 100.0f), pointAt(130.0f, 100.0f));
    EXPECT_TRUE(tree.getPointerState().bKnown);
    EXPECT_EQ(tree.getPointerState().logicalPoint, glm::vec2(130.0f, 100.0f));
    const auto& pointerPath = tree.getPointerPath();
    ASSERT_EQ(pointerPath.size(), 4u);
    EXPECT_EQ(pointerPath[0]->_name, "TreeRoot");
    EXPECT_EQ(pointerPath[1]->_name, "Layer_0");
    EXPECT_EQ(pointerPath[2]->_name, "Panel");
    EXPECT_EQ(pointerPath[3]->_name, "Button");
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::HitTest);
    EXPECT_EQ(tree.getLastRouteTrace().target, "Button");

    tree.setPointerCapture(button.get());
    tree.dispatchEvent(MouseMoveEvent(700.0f, 500.0f), pointAt(700.0f, 500.0f));
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::PointerCapture);
    EXPECT_EQ(tree.getLastRouteTrace().target, "Button");
    EXPECT_EQ(tree.getPointerPath().back(), button.get());

    tree.setFocus(button.get());
    EXPECT_EQ(tree.getFocusPath().back(), button.get());
    KeyPressedEvent keyEvent{};
    keyEvent._keyCode = EKey::K_A;
    EXPECT_EQ(tree.dispatchEvent(keyEvent, pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::Focus);
    EXPECT_EQ(tree.getLastRouteTrace().target, "Button");

    const nlohmann::json dump = dumpWidgetTree(tree);
    EXPECT_TRUE(dump["pointer"]["known"]);
    EXPECT_EQ(dump["pointer"]["path"].back(), "Button");
    EXPECT_EQ(dump["focusPath"].back(), "Button");
    EXPECT_EQ(dump["lastRoute"]["target"], "Button");
}

TEST(WidgetTreeTest, ChildAddedToAttachedParentJoinsItsTree)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto parent = std::make_shared<UIPanel>("Parent");
    FCanvasSlotArgs parentSlot; parentSlot.offset = {40.0f, 40.0f}; parentSlot.fixedSize = {200.0f, 120.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent, parentSlot);

    auto child = makeButton("LateChild", {20.0f, 20.0f}, {80.0f, 32.0f});
    parent->addDetachedChild(child, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            FCanvasSlotArgs slotArgs;
            slotArgs.offset    = {20.0f, 20.0f};
            slotArgs.fixedSize = {80.0f, 32.0f};
            canvas->apply(slotArgs);
        }
    });
    // Placement lives on the parent->child edge, not on the child's own
    // geometry: the canvas host resolves the rect from this slot offset.
    if (auto* slot = dynamic_cast<UICanvasSlot*>(parent->getSlotForChild(*child))) {
        FCanvasSlotArgs slotArgs;
        slotArgs.offset = {20.0f, 20.0f};
        slot->apply(slotArgs);
    }
    EXPECT_TRUE(child->isAttached());
    EXPECT_EQ(child->getTree(), &tree);
    EXPECT_EQ(child->getParent(), parent.get());

    tree.layout();
    EXPECT_EQ(tree.pickAt({80.0f, 80.0f}), child.get());
}

TEST(WidgetTreeTest, AttachWithEdgeInitializerConfiguresParentOwnedSlot)
{
    WidgetTree tree({.width = 200, .height = 100});
    auto panel = std::make_shared<UIPanel>("Panel");
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel).valid());

    auto child = std::make_shared<UIText>("Child");
    const WidgetAttachment attached = tree.attach(*panel, child, [](UIElement&, UISlot& edge) {
        if (auto* slot = edge.as<UICanvasSlot>()) {
            slot->setOffset({12.0f, 8.0f});
            slot->setFixedSize({80.0f, 24.0f});
            slot->setWidthSizeMode(EWidgetSizeMode::Fixed);
            slot->setHeightSizeMode(EWidgetSizeMode::Fixed);
        }
    });
    ASSERT_TRUE(attached.valid());

    const auto* slot = dynamic_cast<const UICanvasSlot*>(panel->getSlotForChild(*child));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(12.0f, 8.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(80.0f, 24.0f));
}

TEST(WidgetTreeTest, TreeRootUsesCanvasSlotsToStretchSystemLayers)
{
    WidgetTree tree({.width = 320, .height = 180});

    UIElement* root = tree.getRoot();
    ASSERT_NE(root, nullptr);
    ASSERT_NE(dynamic_cast<UICanvasLayout*>(root->getLayout()), nullptr);

    for (int i = 0; i < static_cast<int>(WidgetTree::ELayer::Count); ++i) {
        UIElement* layer = tree.getLayer(static_cast<WidgetTree::ELayer>(i));
        ASSERT_NE(layer, nullptr);
        const auto* slot = dynamic_cast<const UICanvasSlot*>(root->getSlotForChild(*layer));
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.0f, 0.0f));
        EXPECT_EQ(slot->getAnchorMax(), glm::vec2(1.0f, 1.0f));
    }

    tree.layout();
    for (int i = 0; i < static_cast<int>(WidgetTree::ELayer::Count); ++i) {
        UIElement* layer = tree.getLayer(static_cast<WidgetTree::ELayer>(i));
        EXPECT_EQ(layer->_layoutRect.pos, glm::vec2(0.0f, 0.0f));
        EXPECT_EQ(layer->_layoutRect.extent, glm::vec2(320.0f, 180.0f));
    }
}

TEST(WidgetTreeTest, AttachToLayerKeepsChildAbsoluteGeometrySemantics)
{
    WidgetTree tree({.width = 320, .height = 180});
    auto panel = std::make_shared<UIPanel>("Panel");
    FCanvasSlotArgs panelSlot; panelSlot.offset = {24.0f, 18.0f}; panelSlot.fixedSize = {90.0f, 40.0f};

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot).valid());
    tree.layout();

    EXPECT_EQ(panel->_layoutRect.pos, glm::vec2(24.0f, 18.0f));
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(90.0f, 40.0f));
    EXPECT_EQ(panel->getParent(), tree.getLayer(WidgetTree::ELayer::Content));
    const auto* slot = dynamic_cast<const UICanvasSlot*>(tree.getLayer(WidgetTree::ELayer::Content)->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(24.0f, 18.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(90.0f, 40.0f));
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(0.0f, 0.0f));
}

TEST(WidgetTreeTest, LayerCanvasSlotTracksPositionUpdatesAfterAttach)
{
    WidgetTree tree({.width = 320, .height = 180});
    auto panel = std::make_shared<UIPanel>("Panel");
    FCanvasSlotArgs panelSlot; panelSlot.offset = {24.0f, 18.0f}; panelSlot.fixedSize = {90.0f, 40.0f};

    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Tooltip), panel, panelSlot).valid());
    auto* panelSlotLive = tree.getLayer(WidgetTree::ELayer::Tooltip)->getSlotForChild(*panel);
    ASSERT_NE(panelSlotLive, nullptr);
    auto* canvasSlot = panelSlotLive->as<UICanvasSlot>();
    ASSERT_NE(canvasSlot, nullptr);
    canvasSlot->setOffset({40.0f, 22.0f});
    canvasSlot->setFixedSize({96.0f, 44.0f});
    tree.layout();

    const auto* slot = dynamic_cast<const UICanvasSlot*>(tree.getLayer(WidgetTree::ELayer::Tooltip)->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(40.0f, 22.0f));
    EXPECT_EQ(slot->getFixedSize(), glm::vec2(96.0f, 44.0f));
    EXPECT_EQ(panel->_layoutRect.pos, glm::vec2(40.0f, 22.0f));
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(96.0f, 44.0f));
}

TEST(WidgetTreeTest, PointerRouteDeliversPreviewTargetThenBubble)
{
    std::vector<std::string> deliveries;
    WidgetTree tree({.width = 400, .height = 300});
    auto parent = std::make_shared<TestRouteWidget>("Parent", deliveries);
    FCanvasSlotArgs parentSlot; parentSlot.offset = {100.0f, 80.0f}; parentSlot.fixedSize = {120.0f, 80.0f};
    parent->_hitFilter = EWidgetHitFilter::Stop;
    parent->bHandleBubble = true;
    auto child = std::make_shared<TestRouteWidget>("Child", deliveries);
    child->_hitFilter = EWidgetHitFilter::Pass;
    child->bHandleTarget = true;

    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent, parentSlot);
    tree.attach(*parent, child, [](UIElement&, UISlot& edge) {
        if (auto* canvas = edge.as<UICanvasSlot>()) {
            canvas->setOffset({20.0f, 20.0f});
            canvas->setFixedSize({60.0f, 30.0f});
        }
    });
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(140.0f, 120.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(deliveries,
              (std::vector<std::string>{
                  "Parent.preview",
                  "Child.target",
                  "Parent.bubble",
              }));

    const auto& trace = tree.getLastRouteTrace();
    EXPECT_EQ(trace.policy, EWidgetRoutePolicy::HitTest);
    EXPECT_EQ(trace.target, "Child");
    ASSERT_EQ(trace.steps.size(), 5u);
    EXPECT_EQ(trace.steps[2].widget, "Parent");
    EXPECT_EQ(trace.steps[2].phase, EWidgetEventRoutePhase::Preview);
    EXPECT_EQ(trace.steps[3].widget, "Child");
    EXPECT_EQ(trace.steps[3].phase, EWidgetEventRoutePhase::Target);
    EXPECT_TRUE(trace.steps[3].bHandled);
    EXPECT_EQ(trace.steps[4].widget, "Parent");
    EXPECT_EQ(trace.steps[4].phase, EWidgetEventRoutePhase::Bubble);
    EXPECT_TRUE(trace.steps[4].bHandled);

    const nlohmann::json dump = dumpWidgetTree(tree);
    EXPECT_EQ(dump["lastRoute"]["steps"][3]["phase"],
              static_cast<int>(EWidgetEventRoutePhase::Target));
    EXPECT_TRUE(dump["lastRoute"]["steps"][4]["handled"]);
    EXPECT_EQ(dump["lastRoute"]["result"], static_cast<int>(EWidgetRouteResult::HandledExclusive));
}

TEST(WidgetTreeTest, ModalOverlayUsesModalRoutePolicyAndCanDetachDuringTarget)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto overlay = std::make_shared<UIPopupOverlay>("ModalOverlay");
    overlay->_bModal = true;
    overlay->open(tree);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(200.0f, 150.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(overlay->isAttached());
    EXPECT_EQ(tree.getLastRouteTrace().policy, EWidgetRoutePolicy::Modal);
    EXPECT_EQ(tree.getLastRouteTrace().target, "ModalOverlay");
    EXPECT_EQ(tree.getLastRouteTrace().result, EWidgetRouteResult::HandledExclusive);

    const nlohmann::json dump = dumpWidgetTree(tree);
    EXPECT_EQ(dump["lastRoute"]["policyName"], "modal");
    EXPECT_EQ(dump["lastRoute"]["resultName"], "handledExclusive");
}

TEST(WidgetTreeTest, ModalOverlayConsumesDismissClickBeforeUnderlyingContent)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       button = makeButton("Content", {150.0f, 120.0f}, {80.0f, 32.0f});
    int        clicks = 0;
    button->_onClick = [&] { ++clicks; };
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({150.0f, 120.0f}, {80.0f, 32.0f}));

    auto overlay = std::make_shared<UIPopupOverlay>("ModalOverlay");
    overlay->_bModal = true;
    overlay->open(tree);
    tree.layout();

    // The dismissing press belongs to the modal shield, not the content
    // underneath. Closing the overlay must not leak this click through.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(170.0f, 130.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(overlay->isAttached());
    EXPECT_EQ(clicks, 0);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(170.0f, 130.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 0);

    // A fresh click after the modal is gone reaches the underlying content.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(170.0f, 130.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(170.0f, 130.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
}

TEST(WidgetTreeTest, PopupOverlayUsesACanvasSlotForItsContentChild)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       overlay = std::make_shared<UIPopupOverlay>("Overlay");
    auto       panel   = std::make_shared<UIPanel>("Content");
    overlay->_contentPos = {24.0f, 18.0f};
    overlay->_contentExtent = {80.0f, 36.0f};
    overlay->addDetachedChild(panel);

    overlay->open(tree);
    tree.layout();

    const auto* slot = dynamic_cast<const UICanvasSlot*>(overlay->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getOffset(), glm::vec2(24.0f, 18.0f));
    EXPECT_EQ(slot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getHeightSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(panel->_layoutRect.pos, glm::vec2(24.0f, 18.0f));
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(80.0f, 36.0f));
}

TEST(WidgetTreeTest, PopupOverlayContentExtentLivesOnTheCanvasSlot)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       overlay = std::make_shared<UIPopupOverlay>("Overlay");
    auto       panel   = std::make_shared<UIPanel>("Content");
    overlay->_contentPos    = {16.0f, 12.0f};
    overlay->_contentExtent = {120.0f, 48.0f};
    overlay->addDetachedChild(panel);

    overlay->open(tree);
    tree.layout();

    const auto* slot = dynamic_cast<const UICanvasSlot*>(overlay->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(120.0f, 48.0f));
    EXPECT_EQ(panel->_layoutRect.pos, glm::vec2(16.0f, 12.0f));
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(120.0f, 48.0f));
}

TEST(WidgetTreeTest, DialogCentresContentThroughThePopupCanvasSlot)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto       content = ui::panel("Body").setSize({120.0f, 40.0f}).release();
    auto dialog = UIDialog::create("Confirm", content);

    dialog->open(tree);
    tree.layout();

    ASSERT_EQ(dialog->getChildren().size(), 1u);
    UIElement* panel = dialog->getChildren()[0].get();
    ASSERT_NE(panel, nullptr);
    const auto* slot = dynamic_cast<const UICanvasSlot*>(dialog->getSlotForChild(*panel));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getAnchorMin(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getAnchorMax(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getPivot(), glm::vec2(0.5f, 0.5f));
    EXPECT_EQ(slot->getWidthSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getHeightSizeMode(), EWidgetSizeMode::Auto);
    EXPECT_EQ(slot->getPreferredSize(), glm::vec2(360.0f, 136.0f));
    EXPECT_FLOAT_EQ(panel->_layoutRect.pos.x, 20.0f);
    EXPECT_FLOAT_EQ(panel->_layoutRect.pos.y, 82.0f);
    EXPECT_FLOAT_EQ(panel->_layoutRect.extent.x, 360.0f);
    EXPECT_FLOAT_EQ(panel->_layoutRect.extent.y, 136.0f);
}

// === Attach / reparent / detach ===


TEST(WidgetTreeTest, TwoIndependentPanelsAttachToContentLayer)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panelA = std::make_shared<UIPanel>("A");
    auto       panelB = std::make_shared<UIPanel>("B");

    auto attachA = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panelA);
    auto attachB = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panelB);

    EXPECT_TRUE(attachA.valid());
    EXPECT_TRUE(attachB.valid());
    EXPECT_TRUE(tree.contains(*panelA));
    EXPECT_TRUE(tree.contains(*panelB));

    UIElement* content = tree.getLayer(WidgetTree::ELayer::Content);
    ASSERT_EQ(content->getChildren().size(), 2u);
    EXPECT_EQ(content->getChildren()[0], panelA);
    EXPECT_EQ(content->getChildren()[1], panelB);

    // Both panels are laid out against the content layer rect.
    tree.layout();
    EXPECT_EQ(panelA->_layoutRect.pos, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(panelB->_layoutRect.pos, glm::vec2(0.0f, 0.0f));
}

TEST(WidgetTreeTest, DetachedWidgetDoesNotParticipate)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("Detached");

    EXPECT_FALSE(panel->isAttached());
    EXPECT_EQ(panel->getTree(), nullptr);
    EXPECT_EQ(panel->getParent(), nullptr);

    tree.layout();
    // Not in the tree: layout never touches it and input never reaches it.
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(30.0f, 30.0f)),
              EWidgetRouteResult::NotHandled);
}

TEST(WidgetTreeTest, AttachTwiceFails)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");

    auto first  = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);
    auto second = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);

    EXPECT_TRUE(first.valid());
    EXPECT_FALSE(second.valid());
    EXPECT_EQ(tree.getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 1u);
}

TEST(WidgetTreeTest, CrossTreeAttachFailsWithoutReparent)
{
    WidgetTree treeA({.width = 800, .height = 600});
    WidgetTree treeB({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");

    auto attachA = treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), button);
    auto attachB = treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), button);

    EXPECT_TRUE(attachA.valid());
    EXPECT_FALSE(attachB.valid());
    EXPECT_TRUE(treeA.contains(*button));
    EXPECT_FALSE(treeB.contains(*button));
}

TEST(WidgetTreeTest, ExplicitReparentMovesWidget)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parentA = std::make_shared<UIPanel>("A");
    auto       parentB = std::make_shared<UIPanel>("B");
    auto       child   = std::make_shared<UIButton>("Child");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parentA);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parentB);
    tree.attach(*parentA, child);

    tree.reparent(*parentB, child);

    EXPECT_EQ(child->getParent(), parentB.get());
    EXPECT_EQ(parentA->getChildren().size(), 0u);
    EXPECT_EQ(parentB->getChildren().size(), 1u);
    EXPECT_TRUE(tree.contains(*child));
}

TEST(WidgetTreeTest, CrossTreeReparentMovesExplicitly)
{
    WidgetTree treeA({.width = 800, .height = 600});
    WidgetTree treeB({.width = 800, .height = 600});
    auto       parentB = std::make_shared<UIPanel>("B");
    auto       child   = std::make_shared<UIButton>("Child");
    treeB.attach(*treeB.getLayer(WidgetTree::ELayer::Content), parentB);
    treeA.attach(*treeA.getLayer(WidgetTree::ELayer::Content), child);

    treeB.reparent(*parentB, child);

    EXPECT_FALSE(treeA.contains(*child));
    EXPECT_TRUE(treeB.contains(*child));
    EXPECT_EQ(child->getParent(), parentB.get());
    EXPECT_EQ(child->getTree(), &treeB);
}

// Sibling-relative moves: order within the same parent is preserved and
// cross-parent moves insert at the sibling position (designer drag-drop).
TEST(WidgetTreeTest, ReparentAfterMovesSiblingForward)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parent = std::make_shared<UIPanel>("Root");
    auto       a      = std::make_shared<UIButton>("A");
    auto       b      = std::make_shared<UIButton>("B");
    auto       c      = std::make_shared<UIButton>("C");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent);
    tree.attach(*parent, a);
    tree.attach(*parent, b);
    tree.attach(*parent, c);

    tree.reparentAfter(*a, c); // C after A -> A, C, B

    EXPECT_EQ(parent->getChildren()[0].get(), a.get());
    EXPECT_EQ(parent->getChildren()[1].get(), c.get());
    EXPECT_EQ(parent->getChildren()[2].get(), b.get());
}

TEST(WidgetTreeTest, ReparentBeforeMovesSiblingBackward)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parent = std::make_shared<UIPanel>("Root");
    auto       a      = std::make_shared<UIButton>("A");
    auto       b      = std::make_shared<UIButton>("B");
    auto       c      = std::make_shared<UIButton>("C");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent);
    tree.attach(*parent, a);
    tree.attach(*parent, b);
    tree.attach(*parent, c);

    tree.reparentBefore(*a, c); // C before A -> C, A, B

    EXPECT_EQ(parent->getChildren()[0].get(), c.get());
    EXPECT_EQ(parent->getChildren()[1].get(), a.get());
    EXPECT_EQ(parent->getChildren()[2].get(), b.get());
}

TEST(WidgetTreeTest, ReparentAfterMovesIntoAnotherParentAtSiblingPosition)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       root    = std::make_shared<UIPanel>("Root");
    auto       other   = std::make_shared<UIPanel>("Other");
    auto       first   = std::make_shared<UIButton>("First");
    auto       second  = std::make_shared<UIButton>("Second");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), root);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), other);
    tree.attach(*other, first);
    tree.attach(*other, second);

    auto moved = std::make_shared<UIButton>("Moved");
    tree.attach(*root, moved);

    // Move `moved` from root into other, after `first`.
    tree.reparentAfter(*first, moved);

    EXPECT_EQ(moved->getParent(), other.get());
    EXPECT_EQ(other->getChildren().size(), 3u);
    EXPECT_EQ(other->getChildren()[0].get(), first.get());
    EXPECT_EQ(other->getChildren()[1].get(), moved.get());
    EXPECT_EQ(other->getChildren()[2].get(), second.get());
    EXPECT_EQ(root->getChildren().size(), 0u);
}

TEST(WidgetTreeTest, ReparentSelfIsNoOp)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       parent = std::make_shared<UIPanel>("Root");
    auto       a      = std::make_shared<UIButton>("A");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), parent);
    tree.attach(*parent, a);

    tree.reparentAfter(*a, a);
    tree.reparentBefore(*a, a);

    EXPECT_EQ(parent->getChildren().size(), 1u);
    EXPECT_EQ(parent->getChildren()[0].get(), a.get());
}

TEST(WidgetTreeTest, ReparentUnderOwnDescendantFails)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       rootPanel = std::make_shared<UIPanel>("Root");
    auto       inner     = std::make_shared<UIPanel>("Inner");
    auto       leaf      = std::make_shared<UIButton>("Leaf");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), rootPanel);
    tree.attach(*rootPanel, inner);
    tree.attach(*inner, leaf);

    // Moving "Root" under its own descendant "Leaf" would create a cycle.
    tree.reparent(*leaf, rootPanel);
    EXPECT_EQ(rootPanel->getParent(), tree.getLayer(WidgetTree::ELayer::Content));
}

TEST(WidgetTreeTest, DetachKeepsBusinessReferenceAlive)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    FCanvasSlotArgs buttonSlot; buttonSlot.offset = {100.0f, 100.0f}; buttonSlot.fixedSize = {80.0f, 32.0f};
    auto       attach = tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    attach.detach();

    EXPECT_FALSE(attach.valid());
    EXPECT_FALSE(button->isAttached());
    EXPECT_EQ(button->getTree(), nullptr);
    EXPECT_EQ(button->getParent(), nullptr);
    // Still alive and detached: no hit, no layout space.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 0u);
}

TEST(WidgetTreeTest, DetachRecursivelyClearsSubtreeMembership)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    auto       child = std::make_shared<UIButton>("C");
    auto       grand = std::make_shared<UIText>("G");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
    tree.attach(*panel, child);
    tree.attach(*child, grand);

    tree.detach(*panel);

    EXPECT_FALSE(panel->isAttached());
    EXPECT_FALSE(child->isAttached());
    EXPECT_FALSE(grand->isAttached());
    // Internal parent links within the detached subtree stay valid.
    EXPECT_EQ(child->getParent(), panel.get());
    EXPECT_EQ(grand->getParent(), child.get());
}

TEST(WidgetTreeTest, DetachClearsFocusCaptureAndHover)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    FCanvasSlotArgs buttonSlot; buttonSlot.offset = {100.0f, 100.0f}; buttonSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.setFocus(button.get());
    tree.setPointerCapture(button.get());
    tree.layout();
    tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f));
    ASSERT_EQ(tree.getHovered(), button.get());

    tree.detach(*button);

    EXPECT_EQ(tree.getFocused(), nullptr);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(tree.getHovered(), nullptr);
}

TEST(WidgetTreeTest, WeakPointerPathsSurviveDetachWithoutDangling)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("Panel");
    FCanvasSlotArgs panelSlot; panelSlot.offset = {100.0f, 80.0f}; panelSlot.fixedSize = {160.0f, 80.0f};
    auto       button = makeButton("Button", {20.0f, 10.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel, panelSlot);
    panel->addDetachedChild(button, [](UIElement&, UISlot& slot) {
        if (auto* canvas = dynamic_cast<UICanvasSlot*>(&slot)) {
            FCanvasSlotArgs args;
            args.offset    = {20.0f, 10.0f};
            args.fixedSize = {80.0f, 32.0f};
            canvas->apply(args);
        }
    });
    tree.setFocus(button.get());
    tree.layout();
    tree.dispatchEvent(MouseMoveEvent(130.0f, 100.0f), pointAt(130.0f, 100.0f));

    // Paths are live snapshots of the weak-reference path (UE FWeakWidgetPath
    // semantics): root -> content layer -> panel -> button.
    EXPECT_EQ(tree.getFocusPath().size(), 4u);
    EXPECT_EQ(tree.getPointerPath().size(), 4u);
    EXPECT_EQ(tree.getFocusPath().back(), button.get());
    EXPECT_EQ(tree.getPointerPath().back(), button.get());

    // Detaching the subtree clears the weak paths; the getters return empty
    // rather than a dangling pointer into the removed widgets.
    tree.detach(*panel);
    EXPECT_TRUE(tree.getFocusPath().empty());
    EXPECT_TRUE(tree.getPointerPath().empty());

    // The detached widgets are now owned only by these business refs; dropping
    // them destroys the widgets while the tree no longer references them. The
    // next dispatch runs pruneTransientState without dereferencing any freed
    // widget (the weak paths already observed their removal).
    button.reset();
    panel.reset();
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(400.0f, 300.0f), pointAt(400.0f, 300.0f)),
              EWidgetRouteResult::NotHandled);
}

TEST(WidgetTreeTest, ButtonTextChildDoesNotStealHoverOwner)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    auto       label  = std::make_shared<UIText>("B_Label");
    label->setText("Render Probe");
    button->addDetachedChild(label);
    FCanvasSlotArgs buttonSlot; buttonSlot.offset = {100.0f, 100.0f}; buttonSlot.fixedSize = {120.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    // Hovering the button (its text child is the raw hit) must resolve hover
    // to the button itself, not the text, so leaving clears the button hover.
    tree.dispatchEvent(MouseMoveEvent(160.0f, 120.0f), pointAt(160.0f, 120.0f));
    EXPECT_EQ(tree.getHovered(), button.get());
    EXPECT_TRUE(button->_bHovered);

    tree.dispatchEvent(MouseMoveEvent(400.0f, 120.0f), pointAt(400.0f, 120.0f));
    EXPECT_EQ(tree.getHovered(), nullptr);
    EXPECT_FALSE(button->_bHovered);
}

TEST(WidgetTreeTest, PopupShieldDoesNotStealHoverOwner)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    FCanvasSlotArgs buttonSlot; buttonSlot.offset = {100.0f, 100.0f}; buttonSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    auto overlay = std::make_shared<UIPopupOverlay>("Overlay");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), overlay);
    tree.layout();

    // A full-screen popup shield is not hoverable: hover under it must still
    // resolve to the interactive button (menu-bar hover-switch relies on this).
    tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f));
    EXPECT_EQ(tree.getHovered(), button.get());
    EXPECT_TRUE(button->_bHovered);

    tree.dispatchEvent(MouseMoveEvent(400.0f, 300.0f), pointAt(400.0f, 300.0f));
    EXPECT_EQ(tree.getHovered(), nullptr);
    EXPECT_FALSE(button->_bHovered);
}

TEST(WidgetTreeTest, TreeDestructionReleasesMembershipSafely)
{
    auto  button = std::make_shared<UIButton>("B");
    auto  panel  = std::make_shared<UIPanel>("P");
    {
        WidgetTree tree({.width = 800, .height = 600});
        tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
        FCanvasSlotArgs buttonSlot; buttonSlot.fixedSize = {80.0f, 32.0f};
        tree.attach(*panel, button, buttonSlot);
        EXPECT_TRUE(panel->isAttached());
        EXPECT_TRUE(button->isAttached());
    }
    // Tree gone: widgets survive via business refs, fully detached, and their
    // destructors must not trip the attached-destruction assert.
    EXPECT_FALSE(panel->isAttached());
    EXPECT_FALSE(button->isAttached());
    EXPECT_EQ(panel->getTree(), nullptr);
    EXPECT_EQ(button->getTree(), nullptr);
    EXPECT_EQ(panel->getSlot(), nullptr);
    EXPECT_EQ(button->getSlot(), nullptr);
}

TEST(WidgetTreeTest, SystemLayersCannotBeDetached)
{
    WidgetTree tree({.width = 800, .height = 600});
    tree.detach(*tree.getLayer(WidgetTree::ELayer::Popup));
    EXPECT_TRUE(tree.contains(*tree.getLayer(WidgetTree::ELayer::Popup)));
}

// === Layout / hit / routing ===

TEST(WidgetTreeTest, ZOrderDefinesHitOrderWithinLayer)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       behind = std::make_shared<UIButton>("Behind");
    auto       front  = std::make_shared<UIButton>("Front");
    behind->_zOrder   = 0;
    front->_zOrder    = 10;
    FCanvasSlotArgs behindSlot; behindSlot.offset = {100.0f, 100.0f}; behindSlot.fixedSize = {80.0f, 32.0f};
    FCanvasSlotArgs frontSlot; frontSlot.offset = {100.0f, 100.0f}; frontSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), behind, behindSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), front, frontSlot);
    tree.layout();

    int behindClicks = 0;
    int frontClicks  = 0;
    behind->_onClick = [&] { ++behindClicks; };
    front->_onClick  = [&] { ++frontClicks; };

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(front->_bHovered);
    EXPECT_FALSE(behind->_bHovered);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(frontClicks, 1);
    EXPECT_EQ(behindClicks, 0);
}

TEST(WidgetTreeTest, SystemLayersStackAboveProjectContent)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       content  = std::make_shared<UIButton>("Content");
    auto       popup    = std::make_shared<UIButton>("Popup");
    auto       tooltip  = std::make_shared<UIButton>("Tooltip");
    auto       dragIme  = std::make_shared<UIButton>("DragIme");
    FCanvasSlotArgs layerSlot; layerSlot.offset = {100.0f, 100.0f}; layerSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), content, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), popup, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Tooltip), tooltip, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::DragIme), dragIme, layerSlot);
    tree.layout();

    int clicks = 0;
    content->_onClick = [&] { clicks = 1; };
    popup->_onClick   = [&] { clicks = 2; };
    tooltip->_onClick = [&] { clicks = 3; };
    dragIme->_onClick = [&] { clicks = 4; };

    // Topmost layer consumes first (drag/ime > tooltip > popup > content).
    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 4);

    tree.detach(*dragIme);
    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 3);

    tree.detach(*tooltip);
    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 2);

    tree.detach(*popup);
    tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 1);
}

TEST(WidgetTreeTest, SystemLayersOwnHoverBeforeLowerLayers)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       content  = std::make_shared<UIButton>("Content");
    auto       popup    = std::make_shared<UIButton>("Popup");
    auto       tooltip  = std::make_shared<UIButton>("Tooltip");
    auto       dragIme  = std::make_shared<UIButton>("DragIme");
    FCanvasSlotArgs layerSlot; layerSlot.offset = {100.0f, 100.0f}; layerSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), content, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), popup, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Tooltip), tooltip, layerSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::DragIme), dragIme, layerSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getHovered(), dragIme.get());
    EXPECT_TRUE(dragIme->_bHovered);
    EXPECT_FALSE(tooltip->_bHovered);
    EXPECT_FALSE(popup->_bHovered);
    EXPECT_FALSE(content->_bHovered);

    tree.detach(*dragIme);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getHovered(), tooltip.get());
    EXPECT_TRUE(tooltip->_bHovered);
    EXPECT_FALSE(popup->_bHovered);
    EXPECT_FALSE(content->_bHovered);

    tree.detach(*tooltip);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getHovered(), popup.get());
    EXPECT_TRUE(popup->_bHovered);
    EXPECT_FALSE(content->_bHovered);
}

TEST(WidgetTreeTest, PassWidgetsRespondButDoNotBlock)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       top    = std::make_shared<UIButton>("Top");
    auto       bottom = std::make_shared<UIButton>("Bottom");
    top->_hitFilter    = EWidgetHitFilter::Pass; // respond, report HandledPass
    top->_zOrder       = 10;
    FCanvasSlotArgs topSlot; topSlot.offset = {100.0f, 100.0f}; topSlot.fixedSize = {80.0f, 32.0f};
    FCanvasSlotArgs bottomSlot = topSlot;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), top, topSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), bottom, bottomSlot);
    tree.layout();

    // Single topmost hit: only the Pass overlay responds and it reports
    // HandledPass (the game layer still sees the event). The Stop widget
    // underneath is NOT reached — hit-testing no longer walks lower
    // candidates; a Pass widget must opt into HitTestInvisible to fall
    // through, not rely on route-time continuation.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledPass);
    EXPECT_TRUE(top->_bPressed);
    EXPECT_FALSE(bottom->_bPressed);
}

TEST(WidgetTreeTest, HiddenSubtreeCullsHits)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = std::make_shared<UIButton>("B");
    button->setVisibility(EWidgetVisibility::Hidden);
    FCanvasSlotArgs buttonSlot; buttonSlot.offset = {100.0f, 100.0f}; buttonSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, buttonSlot);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

// === Focus / capture ===

TEST(WidgetTreeTest, KeyboardEventsRouteToFocusedWidget)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       keyWidget = std::make_shared<TestKeyWidget>("Key");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), keyWidget);
    tree.layout();

    KeyPressedEvent keyEvent{};
    keyEvent._keyCode = EKey::K_A;
    // Not focused: keyboard events are not routed.
    EXPECT_EQ(tree.dispatchEvent(keyEvent, pointAt(120.0f, 110.0f)), EWidgetRouteResult::NotHandled);

    tree.setFocus(keyWidget.get());
    EXPECT_EQ(tree.getFocused(), keyWidget.get());
    // Focused widget receives the event even though the point is outside it.
    EXPECT_EQ(tree.dispatchEvent(keyEvent, pointAt(0.0f, 0.0f)), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(keyWidget->keyHits, 1);
}

TEST(WidgetTreeTest, PointerCaptureOverridesHitWalk)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       captured = std::make_shared<UIButton>("Captured");
    auto       other    = std::make_shared<UIButton>("Other");
    FCanvasSlotArgs capturedSlot; capturedSlot.offset = {100.0f, 100.0f}; capturedSlot.fixedSize = {80.0f, 32.0f};
    FCanvasSlotArgs otherSlot; otherSlot.offset = {300.0f, 300.0f}; otherSlot.fixedSize = {80.0f, 32.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), captured, capturedSlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), other, otherSlot);
    tree.layout();

    int capturedClicks = 0;
    int otherClicks    = 0;
    captured->_onClick = [&] { ++capturedClicks; };
    other->_onClick    = [&] { ++otherClicks; };

    tree.setPointerCapture(captured.get());
    // Press outside both rects: still routed to the captured widget.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(capturedClicks, 1);
    EXPECT_EQ(otherClicks, 0);

    tree.releasePointerCapture(captured.get());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::NotHandled);
}

// === Phase 2: focus traversal + button capture semantics ===

namespace
{

KeyPressedEvent makeKeyPress(EKey::T key, uint32_t mod = 0, bool bRepeat = false)
{
    KeyPressedEvent ev;
    ev._keyCode = key;
    ev._mod     = mod;
    ev.bRepeat  = bRepeat;
    return ev;
}

} // namespace

TEST(WidgetTreeTest, TabTraversalFollowsStablePaintOrderWithWrapAround)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       a = makeButton("A", {0.0f, 0.0f}, {40.0f, 20.0f});
    auto       b = makeButton("B", {0.0f, 40.0f}, {40.0f, 20.0f});
    auto       c = makeButton("C", {0.0f, 80.0f}, {40.0f, 20.0f});
    a->_zOrder   = 10;
    b->_zOrder   = 5; // paint order: B, A, C
    c->_zOrder   = 20;
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), a);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), b);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), c);
    tree.layout();

    const KeyPressedEvent tab      = makeKeyPress(EKey::Tab);
    const KeyPressedEvent shiftTab = makeKeyPress(EKey::Tab, EKeyMod::Shift);
    const auto            at       = pointAt(0.0f, 0.0f);

    // First Tab starts from the front of the stable order (B).
    EXPECT_EQ(tree.dispatchEvent(tab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), b.get());
    EXPECT_TRUE(b->_bFocused);

    EXPECT_EQ(tree.dispatchEvent(tab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), a.get());
    EXPECT_FALSE(b->_bFocused);

    EXPECT_EQ(tree.dispatchEvent(tab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), c.get());

    // Wrap-around: C -> B.
    EXPECT_EQ(tree.dispatchEvent(tab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), b.get());

    // Shift+Tab walks backwards: B -> C -> A -> B.
    EXPECT_EQ(tree.dispatchEvent(shiftTab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), c.get());
    EXPECT_EQ(tree.dispatchEvent(shiftTab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), a.get());
    EXPECT_EQ(tree.dispatchEvent(shiftTab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), b.get());
    EXPECT_EQ(tree.dispatchEvent(shiftTab, at), EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), c.get());
}

TEST(WidgetTreeTest, TabSkipsNonFocusableAndHiddenWidgets)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       plain   = std::make_shared<UIPanel>("Plain"); // never focusable
    auto       hidden  = makeButton("Hidden", {0.0f, 0.0f}, {40.0f, 20.0f});
    auto       visible = makeButton("Visible", {0.0f, 0.0f}, {40.0f, 20.0f});
    hidden->setVisibility(EWidgetVisibility::Hidden); // focusable but not visible
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), plain);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), hidden, makeButtonSlot({0.0f, 0.0f}, {40.0f, 20.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), visible, makeButtonSlot({0.0f, 0.0f}, {40.0f, 20.0f}));
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.getFocused(), visible.get());
}

TEST(WidgetTreeTest, TabWithoutFocusablesIsNotHandled)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Tab), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.getFocused(), nullptr);
}

TEST(WidgetTreeTest, ButtonPressRequestsFocusAndCapture)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({100.0f, 100.0f}, {80.0f, 32.0f}));
    tree.layout();

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(button->_bPressed);
    EXPECT_EQ(tree.getFocused(), button.get());
    // Pointer press requests logical focus but must not light the persistent
    // keyboard-focus highlight (that stays reserved for Tab traversal).
    EXPECT_FALSE(button->_bFocused);
    EXPECT_EQ(tree.getPointerCapture(), button.get());

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(button->_bPressed);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
}

TEST(WidgetTreeTest, ButtonDragOutReleaseFiresClickViaCapture)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       other  = makeButton("Other", {250.0f, 250.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({100.0f, 100.0f}, {80.0f, 32.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), other, makeButtonSlot({250.0f, 250.0f}, {80.0f, 32.0f}));
    tree.layout();

    int buttonClicks = 0;
    int otherClicks  = 0;
    button->_onClick = [&] { ++buttonClicks; };
    other->_onClick  = [&] { ++otherClicks; };

    // Press inside, drag out, release over the other button: the capture
    // session keeps the press and completes the click on release.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(260.0f, 260.0f), pointAt(260.0f, 260.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(button->_bHovered);
    EXPECT_EQ(tree.getHovered(), nullptr); // hover follows the pointer, not the capture

    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(260.0f, 260.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(buttonClicks, 1);
    EXPECT_EQ(otherClicks, 0);
    EXPECT_FALSE(button->_bPressed);
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
}

TEST(WidgetTreeTest, PressRetiresStaleHoverAndReArmsPressedButton)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       first  = makeButton("First", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       second = makeButton("Second", {250.0f, 100.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), first, makeButtonSlot({100.0f, 100.0f}, {80.0f, 32.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), second, makeButtonSlot({250.0f, 100.0f}, {80.0f, 32.0f}));
    tree.layout();

    // Hover then click the first button: it stays hovered (pointer is over
    // it) and holds focus.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(first->_bHovered);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);

    // Pressing the second button without an intervening move must retire the
    // stale hover on the first and arm the pressed button's own hover (logical
    // focus moves, but a pointer press does not light the keyboard highlight).
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(280.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(first->_bHovered);
    EXPECT_FALSE(first->_bFocused);
    EXPECT_TRUE(second->_bPressed);
    EXPECT_TRUE(second->_bHovered);
    EXPECT_FALSE(second->_bFocused);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(280.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_FALSE(second->_bPressed);
    EXPECT_TRUE(second->_bHovered);

    // Moving back re-arms the first and clears the second.
    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(first->_bHovered);
    EXPECT_FALSE(second->_bHovered);
}

TEST(WidgetTreeTest, FocusedButtonActivatesOnEnterAndSpace)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({100.0f, 100.0f}, {80.0f, 32.0f}));
    tree.layout();

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };
    tree.setFocus(button.get());

    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Space), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(clicks, 2);
    // Key repeats do not re-activate (and bubble as NotHandled).
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::Enter, 0, /*bRepeat=*/true), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 2);
    // Other keys are not the button's business: they bubble as NotHandled so
    // the app layer can route them (e.g. list navigation).
    EXPECT_EQ(tree.dispatchEvent(makeKeyPress(EKey::K_A), pointAt(0.0f, 0.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 2);
}

TEST(WidgetTreeTest, DetachWhilePressedClearsButtonTransientState)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button, makeButtonSlot({100.0f, 100.0f}, {80.0f, 32.0f}));
    tree.layout();

    int clicks = 0;
    button->_onClick = [&] { ++clicks; };

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    ASSERT_TRUE(button->_bPressed);
    ASSERT_EQ(tree.getPointerCapture(), button.get());
    ASSERT_EQ(tree.getFocused(), button.get());

    tree.detach(*button);

    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(tree.getFocused(), nullptr);
    EXPECT_FALSE(button->_bPressed);
    EXPECT_FALSE(button->_bFocused);
    EXPECT_FALSE(button->_bHovered);

    // Re-attached button starts clean: a release without a press does not
    // fire the click.
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), button);
    tree.layout();
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(EMouse::Left), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(clicks, 0);
}

TEST(WidgetTreeTest, DragOverDockSetsPointSensitiveDropPreview)
{
    // Dock regression: dragging a dock-panel payload over the dock space must
    // resolve the merge/split preview at the CURRENT pointer (updateDropHover
    // per move) — before the fix, canAcceptDrop computed the preview into a
    // local and setDropHighlight(true) never stored it, so no hint rendered.
    WidgetTree tree({.width = 800, .height = 600});
    auto       ws   = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;
    auto dock = std::make_shared<UIDockSpace>("Dock");
    dock->setWorkspace(ws);
    FCanvasSlotArgs dockArgs;
    dockArgs.anchorMax = {1.0f, 1.0f};
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, dockArgs).valid());

    auto panel = std::make_shared<UIPanel>("P"); // dock panel content
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel); // keep alive
    const DockPanelId id = ws->addPanel("Scene", panel);
    ws->tearOffPanel(id, {120.0f, 120.0f}, {320.0f, 240.0f}); // float it
    ASSERT_TRUE(ws->isPanelFloating(id));
    tree.buildSnapshot(UIFrameBuildContext{}); // cold layout

    // Drag the dock-panel payload over the dock's center (merge band).
    auto source = std::make_shared<UIPanel>("Source");
    FCanvasSlotArgs sourceSlot; sourceSlot.offset = {10.0f, 10.0f}; sourceSlot.fixedSize = {30.0f, 30.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.beginDrag(source.get(), std::string(UIDockSpace::kDockPanelPayload) + std::to_string(id), "Scene",
                   {}, /*bShowGhost=*/true, /*bSkipSourceInHitTest=*/true);
    tree.updateDrag({400.0f, 300.0f}); // center -> merge preview on the leaf
    EXPECT_TRUE(dock->hasDropPreview());
    EXPECT_FALSE(dock->isDropPreviewDisabled());
    EXPECT_TRUE(dock->isDropPreviewMerge());

    const nlohmann::json dump = dumpWidgetTree(tree);
    const auto* overlayNode = findWidgetNode(dump, "DockChooserOverlay");
    ASSERT_NE(overlayNode, nullptr);
    EXPECT_EQ((*overlayNode)["slot"]["type"], "canvas");

    // Moving to the leaf's WEST chooser block switches the preview to a split strip.
    tree.updateDrag({360.0f, 300.0f});
    EXPECT_TRUE(dock->hasDropPreview());
    EXPECT_FALSE(dock->isDropPreviewMerge());
    EXPECT_EQ(dock->getDropPreviewTargetLeafId() != kInvalidDockNodeId, true);

    // The dragged SOURCE now follows the pointer (floating-window drag): the
    // tree skips the drag-source subtree during drop-target discovery, so a
    // window parked AT the pointer must not shadow the dock beneath it.
    if (auto* slot = tree.getLayer(WidgetTree::ELayer::Content)->getSlotForChild(*source)) {
        if (auto* canvas = slot->as<UICanvasSlot>()) canvas->setOffset({390.0f, 290.0f});
    }
    tree.buildSnapshot(UIFrameBuildContext{});
    tree.updateDrag({400.0f, 300.0f}); // pointer over BOTH the source and the dock
    EXPECT_TRUE(dock->hasDropPreview());
    EXPECT_TRUE(dock->isDropPreviewMerge());

    // Leaving the dock clears the preview.
    tree.updateDrag({9000.0f, 9000.0f});
    EXPECT_FALSE(dock->hasDropPreview());
    tree.endDrag({9000.0f, 9000.0f}); // no target: clean finish
}

TEST(WidgetTreeTest, DockPanelPayloadCanMergeIntoFloatingWindowThroughBehaviorTarget)
{
    WidgetTree tree({.width = 1000, .height = 700});
    auto       ws = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;

    auto dock = std::make_shared<UIDockSpace>("Dock");
    FCanvasSlotArgs dockArgs; dockArgs.anchorMin = {0.0f, 0.0f}; dockArgs.anchorMax = {1.0f, 1.0f};
    dock->setWorkspace(ws);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, dockArgs);

    auto panelA = std::make_shared<UIPanel>("PanelA");
    auto panelB = std::make_shared<UIPanel>("PanelB");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panelA);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panelB);
    const DockPanelId panelAId = ws->addPanel("SceneA", panelA);
    const DockPanelId panelBId = ws->addPanel("SceneB", panelB);

    const FDockFloatingWindowId floatingId = ws->tearOffPanel(panelAId, {120.0f, 120.0f}, {320.0f, 240.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);
    auto floating = std::make_shared<UIDockFloatingWindow>("Floating", floatingId, ws);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), floating);
    tree.buildSnapshot(UIFrameBuildContext{});

    auto source = std::make_shared<UIPanel>("Source");
    FCanvasSlotArgs sourceSlot; sourceSlot.offset = {20.0f, 20.0f}; sourceSlot.fixedSize = {30.0f, 30.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, sourceSlot);
    tree.buildSnapshot(UIFrameBuildContext{});

    tree.beginDrag(source.get(), std::string(UIDockSpace::kDockPanelPayload) + std::to_string(panelBId), "SceneB",
                   {}, /*bShowGhost=*/true, /*bSkipSourceInHitTest=*/true);
    tree.updateDrag({180.0f, 180.0f});
    ASSERT_TRUE(tree.isDragging());
    tree.endDrag({180.0f, 180.0f});

    const auto* record = ws->findFloatingById(floatingId);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->panelIds.size(), 2u);
    EXPECT_EQ(record->activePanelId, panelBId);
    EXPECT_TRUE(std::find(record->panelIds.begin(), record->panelIds.end(), panelAId) != record->panelIds.end());
    EXPECT_TRUE(std::find(record->panelIds.begin(), record->panelIds.end(), panelBId) != record->panelIds.end());
}

TEST(WidgetTreeTest, DockSpaceTabDragBehaviorStartsSessionAndTearsOffOnNoTarget)
{
    WidgetTree tree({.width = 1000, .height = 700});
    auto       ws = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;

    auto dock = std::make_shared<UIDockSpace>("Dock");
    FCanvasSlotArgs dockArgs; dockArgs.anchorMin = {0.0f, 0.0f}; dockArgs.anchorMax = {1.0f, 1.0f};
    dock->setWorkspace(ws);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, dockArgs);

    auto panel = std::make_shared<UIPanel>("Panel");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
    const DockPanelId panelId = ws->addPanel("Scene", panel);
    tree.buildSnapshot(UIFrameBuildContext{});

    UITabBar* tabBar = findDescendantOfType<UITabBar>(*dock);
    ASSERT_NE(tabBar, nullptr);
    ASSERT_TRUE(static_cast<bool>(tabBar->_onTabDragBegin));

    tabBar->_onTabDragBegin(0, "Scene");
    ASSERT_TRUE(tree.isDragging());
    EXPECT_EQ(tree.getDragSource(), dock.get());
    EXPECT_EQ(tree.getDragPayload(), std::string(UIDockSpace::kDockPanelPayload) + std::to_string(panelId));

    tree.endDrag({520.0f, 410.0f});
    EXPECT_FALSE(tree.isDragging());
    EXPECT_TRUE(ws->isPanelFloating(panelId));
    const auto* floating = ws->findFloatingByPanel(panelId);
    ASSERT_NE(floating, nullptr);
    EXPECT_EQ(floating->pos, glm::vec2(520.0f, 410.0f));
}

TEST(WidgetTreeTest, FloatingWindowTabDragBehaviorStartsDockPanelSession)
{
    WidgetTree tree({.width = 1000, .height = 700});
    auto       ws = std::make_shared<UIDockWorkspace>();
    ws->bAllowFloating = true;
    ws->bAllowTearOff  = true;

    auto dock = std::make_shared<UIDockSpace>("Dock");
    FCanvasSlotArgs dockArgs; dockArgs.anchorMin = {0.0f, 0.0f}; dockArgs.anchorMax = {1.0f, 1.0f};
    dock->setWorkspace(ws);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), dock, dockArgs);

    auto panel = std::make_shared<UIPanel>("Panel");
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), panel);
    const DockPanelId panelId = ws->addPanel("Scene", panel);
    const FDockFloatingWindowId floatingId = ws->tearOffPanel(panelId, {120.0f, 120.0f}, {320.0f, 240.0f});
    ASSERT_NE(floatingId, kInvalidFloatingWindowId);

    auto floating = std::make_shared<UIDockFloatingWindow>("Floating", floatingId, ws);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Popup), floating);
    tree.buildSnapshot(UIFrameBuildContext{});

    UITabBar* tabBar = findDescendantOfType<UITabBar>(*floating);
    ASSERT_NE(tabBar, nullptr);
    ASSERT_TRUE(static_cast<bool>(tabBar->_onTabDragBegin));

    tabBar->_onTabDragBegin(0, "Scene");
    ASSERT_TRUE(tree.isDragging());
    EXPECT_EQ(tree.getDragSource(), floating.get());
    EXPECT_EQ(tree.getDragPayload(), std::string(UIDockSpace::kDockPanelPayload) + std::to_string(panelId));

    tree.endDrag({9000.0f, 9000.0f});
    EXPECT_FALSE(tree.isDragging());
    EXPECT_TRUE(ws->isPanelFloating(panelId));
}

TEST(WidgetTreeTest, DragObserverReceivesEveryMoveAndTargetChanges)
{
    WidgetTree tree({.width = 500, .height = 300});
    auto source = makeButton("Source", {20.0f, 20.0f}, {80.0f, 30.0f});
    auto targetA = std::make_shared<TestDropTarget>("TargetA");
    FCanvasSlotArgs targetASlot; targetASlot.offset = {150.0f, 40.0f}; targetASlot.fixedSize = {100.0f, 80.0f};
    auto targetB = std::make_shared<TestDropTarget>("TargetB");
    FCanvasSlotArgs targetBSlot; targetBSlot.offset = {300.0f, 40.0f}; targetBSlot.fixedSize = {100.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, makeButtonSlot({20.0f, 20.0f}, {80.0f, 30.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), targetA, targetASlot);
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), targetB, targetBSlot);
    tree.layout();

    std::vector<glm::vec2> moves;
    std::vector<std::string> observations;
    DragSessionObserver observer;
    observer.onMove = [&](const std::string& payload, const glm::vec2& point, std::string_view target) {
        EXPECT_EQ(payload, "panel");
        moves.push_back(point);
        observations.emplace_back(target);
    };
    observer.onTargetChanged = [&](std::string_view previous, std::string_view current) {
        observations.emplace_back(std::string(previous) + "->" + std::string(current));
    };

    tree.beginDrag(source.get(), "panel", "Panel", std::move(observer));
    tree.updateDrag({10.0f, 10.0f});
    tree.updateDrag({170.0f, 60.0f});
    tree.updateDrag({180.0f, 70.0f});
    tree.updateDrag({320.0f, 60.0f});

    ASSERT_EQ(moves.size(), 4u);
    EXPECT_EQ(moves[0], glm::vec2(10.0f, 10.0f));
    EXPECT_EQ(moves[3], glm::vec2(320.0f, 60.0f));
    ASSERT_EQ(observations.size(), 6u);
    EXPECT_EQ(observations[0], "");
    EXPECT_EQ(observations[1], "->TargetA");
    EXPECT_EQ(observations[2], "TargetA");
    EXPECT_EQ(observations[3], "TargetA");
    EXPECT_EQ(observations[4], "TargetA->TargetB");
    EXPECT_EQ(observations[5], "TargetB");
}

TEST(WidgetTreeTest, DragObserverDistinguishesDropNoTargetAndCancel)
{
    WidgetTree tree({.width = 500, .height = 300});
    auto source = makeButton("Source", {20.0f, 20.0f}, {80.0f, 30.0f});
    auto target = std::make_shared<TestDropTarget>("Target");
    FCanvasSlotArgs targetSlot; targetSlot.offset = {150.0f, 40.0f}; targetSlot.fixedSize = {100.0f, 80.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), source, makeButtonSlot({20.0f, 20.0f}, {80.0f, 30.0f}));
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), target, targetSlot);
    tree.layout();

    std::vector<EDragFinishResult> results;
    std::vector<std::string> names;
    auto makeObserver = [&] {
        DragSessionObserver observer;
        observer.onFinished = [&](EDragFinishResult result, const glm::vec2&, std::string_view name) {
            results.push_back(result);
            names.emplace_back(name);
        };
        return observer;
    };

    tree.beginDrag(source.get(), "drop", "Drop", makeObserver());
    tree.endDrag({180.0f, 60.0f});
    EXPECT_FALSE(tree.isDragging());
    EXPECT_EQ(target->drops, 1);
    EXPECT_EQ(target->lastPayload, "drop");

    tree.beginDrag(source.get(), "none", "None", makeObserver());
    tree.endDrag({10.0f, 10.0f});

    tree.beginDrag(source.get(), "cancel", "Cancel", makeObserver());
    tree.updateDrag({180.0f, 60.0f});
    tree.cancelDrag();

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], EDragFinishResult::Dropped);
    EXPECT_EQ(results[1], EDragFinishResult::NoTarget);
    EXPECT_EQ(results[2], EDragFinishResult::Cancelled);
    EXPECT_EQ(names[0], "Target");
    EXPECT_TRUE(names[1].empty());
    EXPECT_TRUE(names[2].empty());
    EXPECT_FALSE(tree.isDragging());
    EXPECT_EQ(tree.getDragSource(), nullptr);
    EXPECT_TRUE(tree.getDragPayload().empty());
}

// === UITypeRegistry ===

TEST(WidgetTreeTest, RegistryExplicitRegistrationAndCreate)
{
    auto& registry = UITypeRegistry::instance();
    registry.registerType(
        {.typeId = "test.inventory_panel", .displayName = "Inventory Panel", .category = "Test"},
        [] { return std::make_shared<UIPanel>("Inventory"); });

    UIElementRef widget = registry.createInstance("test.inventory_panel");
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->_typeId, "test.inventory_panel");
    EXPECT_EQ(widget->_name, "Inventory");
    EXPECT_FALSE(widget->isAttached()); // created detached

    EXPECT_EQ(registry.createInstance("test.missing"), nullptr);
    EXPECT_EQ(registry.findType("test.inventory_panel")->displayName, "Inventory Panel");

    const auto ids = registry.getTypeIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), "test.inventory_panel"), ids.end());

    registry.unregisterType("test.inventory_panel");
    EXPECT_EQ(registry.createInstance("test.inventory_panel"), nullptr);
}

TEST(WidgetTreeTest, RegistryModuleLiveInstanceGuard)
{
    auto& registry = UITypeRegistry::instance();
    auto  module   = registry.beginModule("test.ui_module");
    ASSERT_NE(module, nullptr);
    registry.registerType(
        {.typeId = "test.module_panel", .displayName = "Module Panel", .module = module},
        [] { return std::make_shared<UIPanel>("ModulePanel"); });

    UIElementRef live = registry.createInstance("test.module_panel");
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(module->liveInstances, 1u);

    // Unload must fail while the instance is alive.
    EXPECT_FALSE(registry.endModule(module));
    EXPECT_EQ(module->liveInstances, 1u);

    // Instance destroyed -> module can unload and its types disappear.
    live.reset();
    EXPECT_EQ(module->liveInstances, 0u);
    EXPECT_TRUE(registry.endModule(module));
    EXPECT_EQ(registry.createInstance("test.module_panel"), nullptr);
}

TEST(WidgetTreeTest, RegistryModulesAreSharedOwners)
{
    auto& registry = UITypeRegistry::instance();
    auto  a        = registry.beginModule("test.shared_module");
    auto  b        = registry.beginModule("test.shared_module");
    EXPECT_EQ(a.get(), b.get());
    registry.endModule(a);
    // The second handle still refers to the same (now-ended) module.
    EXPECT_TRUE(registry.endModule(b));
}

} // namespace ya
