// AppKernel regression (shared app foundation). The kernel owns only the loop
// + event pump + timing + exit policy; a headless server/CLI runs it with a
// null event source and null frame sink, proving presentation is not in the
// kernel.

#include "Core/Application/AppKernel.h"
#include "Core/MessageBus.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

struct CountingDelegate final : public IAppLoopDelegate
{
    int  ticks    = 0;
    int  events   = 0;
    bool started  = false;
    bool shutdown = false;
    int  closeAfter = 0;

    void onInit() override { started = true; }
    void onEvent(const Event&) override { ++events; }
    void onTick(float) override { ++ticks; }
    void onShutdown() override { shutdown = true; }
    bool shouldClose() const override { return closeAfter > 0 && ticks >= closeAfter; }
};

struct OneEventSource final : public IAppEventSource
{
    void pollEvents(const std::function<void(const Event&)>& emit) override
    {
        if (bEmitted) {
            return;
        }
        bEmitted = true;
        emit(MouseMoveEvent(10.0f, 20.0f));
    }
    bool bEmitted = false;
};

struct DynamicMouseSubscriber
{
    int count = 0;
    bool onMouseMoved(const MouseMoveEvent&)
    {
        ++count;
        return true;
    }
};

} // namespace

TEST(AppKernelTest, HeadlessLoopHonorsExitAfterFrame)
{
    CountingDelegate delegate;
    AppKernel        kernel({}, delegate); // no event source, no frame sink
    const int        result = kernel.run(AppAutomationRunOptions{.exitAfterFrame = 5});

    EXPECT_EQ(result, 0);
    EXPECT_EQ(delegate.ticks, 5);
    EXPECT_EQ(delegate.events, 0);
    EXPECT_TRUE(delegate.started);
    EXPECT_TRUE(delegate.shutdown);
}

TEST(AppKernelTest, DelegateCloseAndEventSourceDrive)
{
    CountingDelegate delegate;
    delegate.closeAfter = 3;
    OneEventSource   source;
    AppKernel        kernel({.eventSource = &source}, delegate);

    const int result = kernel.run();
    EXPECT_EQ(result, 0);
    EXPECT_EQ(delegate.ticks, 3);
    EXPECT_EQ(delegate.events, 1);
    EXPECT_TRUE(delegate.shutdown);
}

TEST(AppKernelTest, RuntimeTypedEventBridgePublishesConcreteEvent)
{
    DynamicMouseSubscriber subscriber;
    MessageBus::get()->subscribe<MouseMoveEvent>(&subscriber, &DynamicMouseSubscriber::onMouseMoved);

    MouseMoveEvent concrete(10.0f, 20.0f);
    const Event&   erased = concrete;
    MessageBus::get()->publishEvent(erased);

    EXPECT_EQ(subscriber.count, 1);
    MessageBus::get()->unsubscribe(&subscriber);
}

} // namespace ya
