// MulticastDelegate: weak-owner listeners.

#include "Core/Delegate.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

TEST(DelegateTest, WeakListenerEndsWithItsOwner)
{
    MulticastDelegate<void(int)> event;
    std::vector<std::string>     log;
    auto                         owner = std::make_shared<int>(0);

    const DelegateHandle weak = event.addWeakLambda(std::weak_ptr(owner), [&log](int v) { log.push_back("weak:" + std::to_string(v)); });
    event.addLambda([&log](int v) { log.push_back("plain:" + std::to_string(v)); });
    EXPECT_NE(weak, INVALID_HANDLE);
    EXPECT_EQ(event.size(), 2u);

    event.broadcast(1);
    EXPECT_EQ(log, (std::vector<std::string>{"weak:1", "plain:1"}));

    owner.reset();
    EXPECT_EQ(event.size(), 1u) << "an expired owner's listener no longer counts";
    log.clear();
    event.broadcast(2);
    EXPECT_EQ(log, (std::vector<std::string>{"plain:2"}));
    EXPECT_FALSE(event.remove(weak)) << "broadcast dropped the expired listener";
}

TEST(DelegateTest, WeakListenersAreRemovedByOwner)
{
    MulticastDelegate<void()> event;
    int                       calls = 0;
    auto                      a     = std::make_shared<int>(0);
    auto                      b     = std::make_shared<int>(0);

    event.addWeakLambda(std::weak_ptr(a), [&calls]() { ++calls; });
    event.addWeakLambda(std::weak_ptr(a), [&calls]() { ++calls; });
    event.addWeakLambda(std::weak_ptr(b), [&calls]() { calls += 10; });
    EXPECT_EQ(event.removeAll(a.get()), 2u);
    event.broadcast();
    EXPECT_EQ(calls, 10);

    std::weak_ptr<int> gone;
    EXPECT_EQ(event.addWeakLambda(gone, [&calls]() { ++calls; }), INVALID_HANDLE) << "an expired owner adds nothing";
}

TEST(DelegateTest, OwnerDyingMidBroadcastSkipsItsListener)
{
    MulticastDelegate<void()> event;
    std::vector<std::string>  log;
    auto                      owner = std::make_shared<int>(0);

    event.addLambda([&]() {
        log.push_back("killer");
        owner.reset();
    });
    event.addWeakLambda(std::weak_ptr(owner), [&log]() { log.push_back("victim"); });
    event.broadcast();
    EXPECT_EQ(log, (std::vector<std::string>{"killer"}));
}

} // namespace
