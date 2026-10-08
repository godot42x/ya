// Tree tick registry (ui-behavior-capabilities C2): a widget's own tick and each
// IUITickable behaviour are registrations kept by the tree. They join on
// attach, leave on detach, follow runtime switches, run in tree pre-order for
// visible owners only, and may change while the tree is dispatching.

#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/UIAnimation.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

constexpr size_t kChainDepth = 10;

/// `count` plain panels under the content layer, as chains `kChainDepth` deep.
/// Returns the deepest node of the last chain.
UIElement* buildIdleForest(WidgetTree& tree, size_t count)
{
    UIElement* layer = tree.getLayer(WidgetTree::ELayer::Content);
    UIElement* deepest = layer;
    for (size_t built = 0; built < count;) {
        UIElement* parent = layer;
        for (size_t depth = 0; depth < kChainDepth && built < count; ++depth, ++built) {
            auto node = std::make_shared<UICanvasPanel>("Idle");
           (void)tree.attach(*parent, node);
            parent = node.get();
        }
        deepest = parent;
    }
    return deepest;
}

/// Best per-tick wall time in microseconds over a few batches.
double idleTickMicros(WidgetTree& tree)
{
    constexpr int kBatches = 5;
    constexpr int kTicks   = 200;
    tree.tick(0.0f);
    double best = 1e30;
    for (int batch = 0; batch < kBatches; ++batch) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kTicks; ++i) {
            tree.tick(1.0f / 60.0f);
        }
        const std::chrono::duration<double, std::micro> elapsed = std::chrono::steady_clock::now() - start;
        best = std::min(best, elapsed.count() / kTicks);
    }
    return best;
}

/// A widget whose own class ticks through a callback.
struct FTickingPanel final : UICanvasPanel
{
    std::function<void()> onOwnTick;

    explicit FTickingPanel(std::string name) : UICanvasPanel(std::move(name)) { enableTick(); }

    void tick(float) override
    {
        if (onOwnTick) {
            onOwnTick();
        }
    }
};

std::shared_ptr<FTickingPanel> makeTicker(std::string name, std::vector<std::string>& log)
{
    auto widget       = std::make_shared<FTickingPanel>(name);
    widget->onOwnTick = [&log, name]() { log.push_back(name); };
    return widget;
}

std::shared_ptr<UITickBehavior> addTickBehavior(UIElement& widget, std::function<void()> fn)
{
    auto behavior    = std::make_shared<UITickBehavior>();
    behavior->onTick = [fn = std::move(fn)](UIElement&, float) { fn(); };
    widget.addBehavior(behavior);
    return behavior;
}

UIElement& content(WidgetTree& tree)
{
    return *tree.getLayer(WidgetTree::ELayer::Content);
}

} // namespace

TEST(WidgetTreeTickRegistryTest, IdleTickCostDoesNotGrowWithNodeCount)
{
    WidgetTree small({.width = 800, .height = 600});
    buildIdleForest(small, 1000);
    WidgetTree large({.width = 800, .height = 600});
    UIElement* deepest = buildIdleForest(large, 10000);

    // Nothing registered: the tick has nothing to visit at either size.
    EXPECT_EQ(small.getTickRegistrationCount(), 0u);
    EXPECT_EQ(large.getTickRegistrationCount(), 0u);

    const double smallMicros = idleTickMicros(small);
    const double largeMicros = idleTickMicros(large);
    std::printf("[ tick idle ] 1k nodes: %.3f us/tick, 10k nodes: %.3f us/tick\n", smallMicros, largeMicros);
    // A per-node walk is ~10x here; the registry is the same constant at both sizes.
    EXPECT_LT(largeMicros, smallMicros * 3.0 + 1.0);

    // One registration ten levels down costs one call, not a walk to it.
    int calls = 0;
    addTickBehavior(*deepest, [&calls]() { ++calls; });
    EXPECT_EQ(large.getTickRegistrationCount(), 1u);
    large.tick(1.0f / 60.0f);
    large.tick(1.0f / 60.0f);
    EXPECT_EQ(calls, 2);
}

TEST(WidgetTreeTickRegistryTest, RegistrationsFollowMembershipAndRuntimeSwitches)
{
    WidgetTree tree({.width = 400, .height = 300});
    std::vector<std::string> log;
    auto parent = makeTicker("Parent", log);
    auto child  = makeTicker("Child", log);
    parent->addDetachedChild(child);
    int behaviorTicks = 0;
    auto behavior     = addTickBehavior(*child, [&behaviorTicks]() { ++behaviorTicks; });

    // Detached: nothing in the tree yet; attaching registers the whole subtree.
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);
    ASSERT_TRUE(tree.attach(content(tree), parent).valid());
    EXPECT_EQ(tree.getTickRegistrationCount(), 3u);

    // Runtime switches add and remove single registrations.
    child->enableTick(false);
    EXPECT_EQ(tree.getTickRegistrationCount(), 2u);
    behavior->setTickEnabled(false);
    EXPECT_EQ(tree.getTickRegistrationCount(), 1u);
    behavior->setTickEnabled(true);
    child->enableTick(true);
    EXPECT_EQ(tree.getTickRegistrationCount(), 3u);

    // Removing a behaviour unregisters it; adding one to an attached widget registers it.
    child->removeBehavior(*behavior);
    EXPECT_EQ(tree.getTickRegistrationCount(), 2u);
    child->addBehavior(behavior);
    EXPECT_EQ(tree.getTickRegistrationCount(), 3u);

    // Detaching the subtree drops every registration under it; re-attach restores them.
    tree.detach(*parent);
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);
    tree.tick(1.0f / 60.0f);
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(behaviorTicks, 0);
    ASSERT_TRUE(tree.attach(content(tree), parent).valid());
    EXPECT_EQ(tree.getTickRegistrationCount(), 3u);
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"Parent", "Child"}));
    EXPECT_EQ(behaviorTicks, 1);

    // A switched-off registration stays off across detach / attach.
    child->enableTick(false);
    tree.detach(*parent);
    ASSERT_TRUE(tree.attach(content(tree), parent).valid());
    EXPECT_EQ(tree.getTickRegistrationCount(), 2u);

    // Moving to another tree moves the registrations with it.
    WidgetTree other({.width = 400, .height = 300});
    other.reparent(content(other), parent);
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);
    EXPECT_EQ(other.getTickRegistrationCount(), 2u);
    other.detach(*parent);
}

TEST(WidgetTreeTickRegistryTest, TicksRunInTreePreOrderAfterStructureChanges)
{
    WidgetTree tree({.width = 400, .height = 300});
    std::vector<std::string> log;
    auto a  = makeTicker("A", log);
    auto a1 = makeTicker("A1", log);
    auto a2 = makeTicker("A2", log);
    auto b  = makeTicker("B", log);
    // Registered out of tree order on purpose: B first, then A's subtree.
    ASSERT_TRUE(tree.attach(content(tree), b).valid());
    ASSERT_TRUE(tree.attach(content(tree), a).valid());
    ASSERT_TRUE(tree.attach(*a, a2).valid());
    tree.reparentBefore(*a2, a1);
    // An owner's own tick runs before its behaviours.
    addTickBehavior(*a, [&log]() { log.push_back("A.behaviour"); });

    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"B", "A", "A.behaviour", "A1", "A2"}));

    // Sibling reorder and reparent re-derive the order on the next tick.
    log.clear();
    tree.reparentBefore(*b, a);
    tree.reparent(*a1, a2);
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"A", "A.behaviour", "A1", "A2", "B"}));
}

TEST(WidgetTreeTickRegistryTest, OnlyOwnersVisibleInTheTreeTick)
{
    WidgetTree tree({.width = 400, .height = 300});
    std::vector<std::string> log;
    auto shell = std::make_shared<UICanvasPanel>("Shell");
    auto first = makeTicker("First", log);
    auto inner = makeTicker("Inner", log);
    ASSERT_TRUE(tree.attach(content(tree), first).valid());
    ASSERT_TRUE(tree.attach(content(tree), shell).valid());
    ASSERT_TRUE(tree.attach(*shell, inner).valid());

    shell->setVisibility(EWidgetVisibility::Collapsed);
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"First"}));

    shell->setVisibility(EWidgetVisibility::Visible);
    log.clear();
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"First", "Inner"}));

    // Hidden during the frame by an earlier tick: the later entry sees it now.
    first->onOwnTick = [&log, &shell]() {
        log.push_back("First");
        shell->setVisibility(EWidgetVisibility::Hidden);
    };
    log.clear();
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"First"}));
}

TEST(WidgetTreeTickRegistryTest, RegistrationsMayChangeWhileTheTreeDispatches)
{
    WidgetTree tree({.width = 400, .height = 300});
    std::vector<std::string> log;
    auto self     = makeTicker("Self", log);
    auto victim   = makeTicker("Victim", log);
    auto dropped  = std::make_shared<UICanvasPanel>("Dropped");
    auto spawner  = makeTicker("Spawner", log);
    ASSERT_TRUE(tree.attach(content(tree), self).valid());
    ASSERT_TRUE(tree.attach(content(tree), spawner).valid());
    ASSERT_TRUE(tree.attach(content(tree), victim).valid());
    ASSERT_TRUE(tree.attach(content(tree), dropped).valid());
    std::weak_ptr<UITickBehavior> droppedBehavior =
        addTickBehavior(*dropped, [&log]() { log.push_back("Dropped"); });

    // Self switches itself off; Spawner detaches a later widget (its last
    // owner reference goes away), removes a later behaviour, and registers a
    // new widget plus a behaviour on itself.
    self->onOwnTick = [&log, &self]() {
        log.push_back("Self");
        self->enableTick(false);
    };
    std::shared_ptr<FTickingPanel> spawned;
    spawner->onOwnTick = [&]() {
        log.push_back("Spawner");
        if (victim) {
            tree.detach(*victim);
            victim.reset();
            if (UITickBehavior* behavior = dropped->findBehavior<UITickBehavior>()) {
                dropped->removeBehavior(*behavior);
            }
            spawned = makeTicker("Spawned", log);
            ASSERT_TRUE(tree.attach(content(tree), spawned).valid());
            addTickBehavior(*spawner, [&log]() { log.push_back("Spawner.behaviour"); });
        }
    };

    tree.tick(1.0f / 60.0f);
    // Removed entries are skipped for the rest of the frame; added ones wait.
    EXPECT_EQ(log, (std::vector<std::string>{"Self", "Spawner"}));
    EXPECT_TRUE(droppedBehavior.expired());

    log.clear();
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"Spawner", "Spawner.behaviour", "Spawned"}));
    EXPECT_EQ(tree.getTickRegistrationCount(), 3u);

    // A tick that detaches its own owner.
    spawned->onOwnTick = [&log, &tree, &spawned]() {
        log.push_back("Spawned");
        tree.detach(*spawned);
    };
    log.clear();
    tree.tick(1.0f / 60.0f);
    tree.tick(1.0f / 60.0f);
    EXPECT_EQ(log, (std::vector<std::string>{"Spawner", "Spawner.behaviour", "Spawned", "Spawner", "Spawner.behaviour"}));
}

TEST(WidgetTreeTickRegistryTest, AnimatorIsRegisteredOnlyWhileATweenPlays)
{
    WidgetTree tree({.width = 400, .height = 300});
    auto card = std::make_shared<UICanvasPanel>("Card");
    ASSERT_TRUE(tree.attach(content(tree), card).valid());

    std::shared_ptr<UITween> tween = animate(*card, 0.5f);
    tween->fade(0.0f, 1.0f);
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);

    tween->play();
    EXPECT_EQ(tree.getTickRegistrationCount(), 1u);
    tree.tick(0.25f);
    EXPECT_EQ(tree.getTickRegistrationCount(), 1u);
    tree.tick(0.25f);
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);
    EXPECT_FLOAT_EQ(card->getRenderOpacity(), 1.0f);

    // Retargeting from rest registers again; stop() unregisters.
    tween->playToward(EUIAnimDirection::Backward);
    EXPECT_EQ(tree.getTickRegistrationCount(), 1u);
    tween->stop();
    EXPECT_EQ(tree.getTickRegistrationCount(), 0u);
}

} // namespace ya
