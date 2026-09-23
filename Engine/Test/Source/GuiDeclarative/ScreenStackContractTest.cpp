#include "GUI/Declarative/ScreenStack.h"
#include "GUI/Declarative/UIScreen.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{

class FakeScreen final : public ya::ui::UIScreen
{
  public:
    explicit FakeScreen(std::string name, int zOrder = 0, ya::ui::EInputBlocking blocking = ya::ui::EInputBlocking::PassThrough)
        : _name(std::move(name))
        , _zOrder(zOrder)
        , _blocking(blocking)
    {
    }

    [[nodiscard]] int                    getZOrder() const override { return _zOrder; }
    [[nodiscard]] ya::ui::EInputBlocking getInputBlocking() const override { return _blocking; }

    [[nodiscard]] const std::string& name() const { return _name; }

  private:
    std::string            _name;
    int                    _zOrder;
    ya::ui::EInputBlocking _blocking;
};

} // namespace

TEST(ScreenStackContractTest, PushOrdersByZOrderFrontmostIsTop)
{
    ya::WidgetTree tree;
    ya::ui::ScreenStack stack(tree);

    auto back   = std::make_shared<FakeScreen>("back", 0);
    auto middle = std::make_shared<FakeScreen>("middle", 10);
    auto front  = std::make_shared<FakeScreen>("front", 100);

    stack.push(back);
    stack.push(middle);
    stack.push(front);

    ASSERT_EQ(stack.size(), 3u);
    ASSERT_NE(stack.top(), nullptr);
    EXPECT_EQ(static_cast<FakeScreen*>(stack.top())->name(), "front");
}

TEST(ScreenStackContractTest, PopUnmountsFrontmost)
{
    ya::WidgetTree tree;
    ya::ui::ScreenStack stack(tree);

    auto a = std::make_shared<FakeScreen>("a", 0);
    auto b = std::make_shared<FakeScreen>("b", 1);
    stack.push(a);
    stack.push(b);

    stack.pop();
    ASSERT_EQ(stack.size(), 1u);
    ASSERT_NE(stack.top(), nullptr);
    EXPECT_EQ(static_cast<FakeScreen*>(stack.top())->name(), "a");
    EXPECT_FALSE(b->isMounted());
    EXPECT_TRUE(a->isMounted());
}

TEST(ScreenStackContractTest, RemoveDropsSpecificScreenByIdentity)
{
    ya::WidgetTree tree;
    ya::ui::ScreenStack stack(tree);

    auto a = std::make_shared<FakeScreen>("a", 0);
    auto b = std::make_shared<FakeScreen>("b", 1);
    stack.push(a);
    stack.push(b);

    stack.remove(*a);
    ASSERT_EQ(stack.size(), 1u);
    EXPECT_EQ(static_cast<FakeScreen*>(stack.top())->name(), "b");
    EXPECT_FALSE(a->isMounted());
    EXPECT_TRUE(b->isMounted());
}

TEST(ScreenStackContractTest, ModalFrontmostBlocksAllInput)
{
    ya::WidgetTree tree;
    ya::ui::ScreenStack stack(tree);

    auto passthrough = std::make_shared<FakeScreen>("hud", 0, ya::ui::EInputBlocking::PassThrough);
    stack.push(passthrough);

    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::NotHandled), ya::EWidgetRouteResult::NotHandled);
    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::HandledPass), ya::EWidgetRouteResult::HandledPass);

    auto modal = std::make_shared<FakeScreen>("pause", 100, ya::ui::EInputBlocking::Block);
    stack.push(modal);

    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::NotHandled), ya::EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::HandledPass), ya::EWidgetRouteResult::HandledExclusive);
    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::HandledExclusive), ya::EWidgetRouteResult::HandledExclusive);

    stack.pop();
    EXPECT_EQ(stack.routeEvent(ya::EWidgetRouteResult::HandledPass), ya::EWidgetRouteResult::HandledPass);
}
