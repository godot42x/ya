// Behaviour capabilities (ui-behavior-capabilities C1): a behaviour joins the
// per-capability lists it declares through UIBehaviorWith<...>, dispatch reads
// those lists in attach order, and a widget holds one behaviour per type.

#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{
namespace
{

template <int N>
struct TickTag final : UIBehaviorWith<IUITickable>
{
    std::vector<int>* log = nullptr;

    void tick(UIElement&, float) override { log->push_back(N); }
};

template <int N>
struct ActionTag final : UIBehaviorWith<IUIActionHandler>
{
    std::vector<int>*               log = nullptr;
    std::function<void(UIElement&)> onHandle;

    bool onAction(UIElement& owner, UIElement&, std::string_view) override
    {
        log->push_back(N);
        if (onHandle) {
            onHandle(owner);
        }
        return false;
    }
};

struct TickAndAction final : UIBehaviorWith<IUITickable, IUIActionHandler>
{
    void tick(UIElement&, float) override {}
    bool onAction(UIElement&, UIElement&, std::string_view) override { return false; }
};

struct NoCapability final : UIBehaviorWith<>
{
};

std::shared_ptr<UIBorder> attachCard(WidgetTree& tree)
{
    auto            card = std::make_shared<UIBorder>("Card");
    FCanvasSlotArgs slot;
    slot.fixedSize = {80.0f, 40.0f};
    tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), card, slot);
    return card;
}

} // namespace

TEST(UIBehaviorCapabilityTest, CapabilitiesJoinTheIndexAsDeclared)
{
    auto card  = std::make_shared<UIBorder>("Card");
    auto both  = std::make_shared<TickAndAction>();
    auto plain = std::make_shared<NoCapability>();
    card->addBehavior(both);
    card->addBehavior(plain);

    EXPECT_TRUE(both->hasCapability(EUIBehaviorCapability::Tick));
    EXPECT_TRUE(both->hasCapability(EUIBehaviorCapability::Action));
    EXPECT_FALSE(both->hasCapability(EUIBehaviorCapability::DropTarget));
    EXPECT_FALSE(plain->hasCapability(EUIBehaviorCapability::Tick));

    ASSERT_EQ(card->behaviorsOf<IUITickable>().size(), 1u);
    EXPECT_EQ(card->behaviorsOf<IUITickable>()[0], static_cast<IUITickable*>(both.get()));
    ASSERT_EQ(card->behaviorsOf<IUIActionHandler>().size(), 1u);
    EXPECT_EQ(card->behaviorsOf<IUIActionHandler>()[0], static_cast<IUIActionHandler*>(both.get()));
    EXPECT_TRUE(card->behaviorsOf<IUIInputHandler>().empty());
    EXPECT_TRUE(card->behaviorsOf<IUIDragSource>().empty());
    EXPECT_TRUE(card->behaviorsOf<IUIDropTarget>().empty());
    EXPECT_EQ(card->getBehaviors().size(), 2u);
}

TEST(UIBehaviorCapabilityTest, RemovedBehaviourLeavesEveryList)
{
    auto card = std::make_shared<UIBorder>("Card");
    auto both = std::make_shared<TickAndAction>();
    card->addBehavior(both);
    card->removeBehavior(*both);

    EXPECT_TRUE(card->behaviorsOf<IUITickable>().empty());
    EXPECT_TRUE(card->behaviorsOf<IUIActionHandler>().empty());
    EXPECT_FALSE(card->hasBehavior(*both));
    EXPECT_FALSE(card->wantsTick());
}

TEST(UIBehaviorCapabilityTest, DispatchFollowsAttachOrder)
{
    WidgetTree       tree({.width = 400, .height = 200});
    auto             card = attachCard(tree);
    std::vector<int> log;

    auto second = std::make_shared<TickTag<2>>();
    auto first  = std::make_shared<TickTag<1>>();
    second->log = &log;
    first->log  = &log;
    card->addBehavior(second);
    card->addBehavior(first);
    tree.tick(0.016f);
    EXPECT_EQ(log, (std::vector<int>{2, 1}));

    log.clear();
    auto actionA = std::make_shared<ActionTag<10>>();
    auto actionB = std::make_shared<ActionTag<20>>();
    actionA->log = &log;
    actionB->log = &log;
    card->addBehavior(actionA);
    card->addBehavior(actionB);
    EXPECT_FALSE(tree.emitAction(*card, "go"));
    EXPECT_EQ(log, (std::vector<int>{10, 20}));
}

TEST(UIBehaviorCapabilityTest, ActionHandlerRemovedMidDispatchIsSkipped)
{
    WidgetTree       tree({.width = 400, .height = 200});
    auto             card = attachCard(tree);
    std::vector<int> log;

    auto remover = std::make_shared<ActionTag<1>>();
    auto removed = std::make_shared<ActionTag<2>>();
    remover->log      = &log;
    removed->log      = &log;
    remover->onHandle = [removed](UIElement& owner) { owner.removeBehavior(*removed); };
    card->addBehavior(remover);
    card->addBehavior(removed);

    EXPECT_FALSE(tree.emitAction(*card, "go"));
    EXPECT_EQ(log, (std::vector<int>{1}));
}

TEST(UIBehaviorCapabilityTest, SecondBehaviourOfTheSameTypeIsRejected)
{
    auto card   = std::make_shared<UIBorder>("Card");
    auto first  = std::make_shared<TickTag<1>>();
    auto second = std::make_shared<TickTag<1>>();

    EXPECT_TRUE(card->addBehavior(first));
    EXPECT_FALSE(card->addBehavior(second));
    EXPECT_TRUE(card->addBehavior(first)) << "re-adding the same instance is a no-op";
    EXPECT_EQ(card->getBehaviors().size(), 1u);
    EXPECT_EQ(card->behaviorsOf<IUITickable>().size(), 1u);
    EXPECT_EQ(card->findBehavior<TickTag<1>>(), first);

    // Another type is a different behaviour; the rule is per concrete type.
    EXPECT_TRUE(card->addBehavior(std::make_shared<TickTag<2>>()));
    EXPECT_EQ(card->findBehavior<TickTag<3>>(), nullptr);

    card->removeBehavior(*first);
    EXPECT_TRUE(card->addBehavior(second));
    EXPECT_EQ(card->findBehavior<TickTag<1>>(), second);
}

} // namespace ya
