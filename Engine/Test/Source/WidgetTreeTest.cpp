// Phase 1 regression guards for the Game UI WidgetTree (ui-widget-tree-refactor):
// single-parent contract, detached lifecycle, zOrder/layer hit order, focus /
// capture, tree teardown, and the UITypeRegistry module live-instance guard.
// The target links ONLY the GUI closure, proving ya-gui-widgets has no
// Scene/ECS/Render3D/Host dependency.

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/UITypeRegistry.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"

#include <gtest/gtest.h>

#include <memory>

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

std::shared_ptr<UIButton> makeButton(const std::string& name, glm::vec2 pos, glm::vec2 size)
{
    auto button        = std::make_shared<UIButton>(name);
    button->_position  = pos;
    button->_size      = size;
    return button;
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

} // namespace

// === Attach / reparent / detach ===

TEST(WidgetTreeTest, TwoIndependentPanelsAttachToContentLayer)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panelA = std::make_shared<UIPanel>("A");
    auto       panelB = std::make_shared<UIPanel>("B");

    auto attachA = tree.attachToLayer(WidgetTree::ELayer::Content, panelA);
    auto attachB = tree.attachToLayer(WidgetTree::ELayer::Content, panelB);

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
    panel->_position  = {10.0f, 10.0f};
    panel->_size      = {100.0f, 50.0f};

    EXPECT_FALSE(panel->isAttached());
    EXPECT_EQ(panel->getTree(), nullptr);
    EXPECT_EQ(panel->getParent(), nullptr);

    tree.layout();
    // Not in the tree: layout never touches it and input never reaches it.
    EXPECT_EQ(panel->_layoutRect.extent, glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(30.0f, 30.0f)),
              EWidgetRouteResult::NotHandled);
}

TEST(WidgetTreeTest, AttachTwiceFails)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {0.0f, 0.0f}, {80.0f, 32.0f});

    auto first  = tree.attachToLayer(WidgetTree::ELayer::Content, button);
    auto second = tree.attachToLayer(WidgetTree::ELayer::Content, button);

    EXPECT_TRUE(first.valid());
    EXPECT_FALSE(second.valid());
    EXPECT_EQ(tree.getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 1u);
}

TEST(WidgetTreeTest, CrossTreeAttachFailsWithoutReparent)
{
    WidgetTree treeA({.width = 800, .height = 600});
    WidgetTree treeB({.width = 800, .height = 600});
    auto       button = makeButton("B", {0.0f, 0.0f}, {80.0f, 32.0f});

    auto attachA = treeA.attachToLayer(WidgetTree::ELayer::Content, button);
    auto attachB = treeB.attachToLayer(WidgetTree::ELayer::Content, button);

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
    auto       child   = makeButton("Child", {0.0f, 0.0f}, {80.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, parentA);
    tree.attachToLayer(WidgetTree::ELayer::Content, parentB);
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
    auto       child   = makeButton("Child", {0.0f, 0.0f}, {80.0f, 32.0f});
    treeB.attachToLayer(WidgetTree::ELayer::Content, parentB);
    treeA.attachToLayer(WidgetTree::ELayer::Content, child);

    treeB.reparent(*parentB, child);

    EXPECT_FALSE(treeA.contains(*child));
    EXPECT_TRUE(treeB.contains(*child));
    EXPECT_EQ(child->getParent(), parentB.get());
    EXPECT_EQ(child->getTree(), &treeB);
}

TEST(WidgetTreeTest, ReparentUnderOwnDescendantFails)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       rootPanel = std::make_shared<UIPanel>("Root");
    auto       inner     = std::make_shared<UIPanel>("Inner");
    auto       leaf      = makeButton("Leaf", {0.0f, 0.0f}, {80.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, rootPanel);
    tree.attach(*rootPanel, inner);
    tree.attach(*inner, leaf);

    // Moving "Root" under its own descendant "Leaf" would create a cycle.
    tree.reparent(*leaf, rootPanel);
    EXPECT_EQ(rootPanel->getParent(), tree.getLayer(WidgetTree::ELayer::Content));
}

TEST(WidgetTreeTest, DetachKeepsBusinessReferenceAlive)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       attach = tree.attachToLayer(WidgetTree::ELayer::Content, button);
    tree.layout();

    attach.detach();

    EXPECT_FALSE(attach.valid());
    EXPECT_FALSE(button->isAttached());
    EXPECT_EQ(button->getTree(), nullptr);
    EXPECT_EQ(button->getParent(), nullptr);
    // Still alive and detached: no hit, no layout space.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_EQ(tree.getLayer(WidgetTree::ELayer::Content)->getChildren().size(), 0u);
}

TEST(WidgetTreeTest, DetachRecursivelyClearsSubtreeMembership)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       panel = std::make_shared<UIPanel>("P");
    auto       child = makeButton("C", {0.0f, 0.0f}, {80.0f, 32.0f});
    auto       grand = std::make_shared<UIText>("G");
    tree.attachToLayer(WidgetTree::ELayer::Content, panel);
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
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
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

TEST(WidgetTreeTest, TreeDestructionReleasesMembershipSafely)
{
    auto  button = makeButton("B", {0.0f, 0.0f}, {80.0f, 32.0f});
    auto  panel  = std::make_shared<UIPanel>("P");
    {
        WidgetTree tree({.width = 800, .height = 600});
        tree.attachToLayer(WidgetTree::ELayer::Content, panel);
        tree.attach(*panel, button);
        EXPECT_TRUE(panel->isAttached());
        EXPECT_TRUE(button->isAttached());
    }
    // Tree gone: widgets survive via business refs, fully detached, and their
    // destructors must not trip the attached-destruction assert.
    EXPECT_FALSE(panel->isAttached());
    EXPECT_FALSE(button->isAttached());
    EXPECT_EQ(panel->getTree(), nullptr);
    EXPECT_EQ(button->getTree(), nullptr);
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
    auto       behind = makeButton("Behind", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       front  = makeButton("Front", {100.0f, 100.0f}, {80.0f, 32.0f});
    behind->_zOrder   = 0;
    front->_zOrder    = 10;
    tree.attachToLayer(WidgetTree::ELayer::Content, behind);
    tree.attachToLayer(WidgetTree::ELayer::Content, front);
    tree.layout();

    int behindClicks = 0;
    int frontClicks  = 0;
    behind->_onClick = [&] { ++behindClicks; };
    front->_onClick  = [&] { ++frontClicks; };

    EXPECT_EQ(tree.dispatchEvent(MouseMoveEvent(120.0f, 110.0f), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(front->_bHovered);
    EXPECT_FALSE(behind->_bHovered);

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(frontClicks, 1);
    EXPECT_EQ(behindClicks, 0);
}

TEST(WidgetTreeTest, SystemLayersStackAboveProjectContent)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       content  = makeButton("Content", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       popup    = makeButton("Popup", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       tooltip  = makeButton("Tooltip", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       dragIme  = makeButton("DragIme", {100.0f, 100.0f}, {80.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, content);
    tree.attachToLayer(WidgetTree::ELayer::Popup, popup);
    tree.attachToLayer(WidgetTree::ELayer::Tooltip, tooltip);
    tree.attachToLayer(WidgetTree::ELayer::DragIme, dragIme);
    tree.layout();

    int clicks = 0;
    content->_onClick = [&] { clicks = 1; };
    popup->_onClick   = [&] { clicks = 2; };
    tooltip->_onClick = [&] { clicks = 3; };
    dragIme->_onClick = [&] { clicks = 4; };

    // Topmost layer consumes first (drag/ime > tooltip > popup > content).
    tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 4);

    tree.detach(*dragIme);
    tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 3);

    tree.detach(*tooltip);
    tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 2);

    tree.detach(*popup);
    tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f));
    tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(120.0f, 110.0f));
    EXPECT_EQ(clicks, 1);
}

TEST(WidgetTreeTest, PassWidgetsRespondButDoNotBlock)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       top    = makeButton("Top", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       bottom = makeButton("Bottom", {100.0f, 100.0f}, {80.0f, 32.0f});
    top->_hitFilter    = EWidgetHitFilter::Pass; // respond but never block
    top->_zOrder       = 10;
    tree.attachToLayer(WidgetTree::ELayer::Content, top);
    tree.attachToLayer(WidgetTree::ELayer::Content, bottom);
    tree.layout();

    // The Pass overlay responds and the walk continues; only the Stop widget
    // underneath makes the route exclusive.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_TRUE(top->_bPressed);
    EXPECT_TRUE(bottom->_bPressed);
}

TEST(WidgetTreeTest, HiddenSubtreeCullsHits)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       button = makeButton("B", {100.0f, 100.0f}, {80.0f, 32.0f});
    button->_visibility = EWidgetVisibility::Hidden;
    tree.attachToLayer(WidgetTree::ELayer::Content, button);
    tree.layout();

    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(120.0f, 110.0f)),
              EWidgetRouteResult::NotHandled);
    EXPECT_FALSE(button->_bPressed);
}

// === Focus / capture ===

TEST(WidgetTreeTest, KeyboardEventsRouteToFocusedWidget)
{
    WidgetTree tree({.width = 800, .height = 600});
    auto       keyWidget = std::make_shared<TestKeyWidget>("Key");
    tree.attachToLayer(WidgetTree::ELayer::Content, keyWidget);
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
    auto       captured = makeButton("Captured", {100.0f, 100.0f}, {80.0f, 32.0f});
    auto       other    = makeButton("Other", {300.0f, 300.0f}, {80.0f, 32.0f});
    tree.attachToLayer(WidgetTree::ELayer::Content, captured);
    tree.attachToLayer(WidgetTree::ELayer::Content, other);
    tree.layout();

    int capturedClicks = 0;
    int otherClicks    = 0;
    captured->_onClick = [&] { ++capturedClicks; };
    other->_onClick    = [&] { ++otherClicks; };

    tree.setPointerCapture(captured.get());
    // Press outside both rects: still routed to the captured widget.
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonReleasedEvent(0), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(capturedClicks, 1);
    EXPECT_EQ(otherClicks, 0);

    tree.releasePointerCapture(captured.get());
    EXPECT_EQ(tree.getPointerCapture(), nullptr);
    EXPECT_EQ(tree.dispatchEvent(MouseButtonPressedEvent(0), pointAt(10.0f, 10.0f)),
              EWidgetRouteResult::NotHandled);
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
