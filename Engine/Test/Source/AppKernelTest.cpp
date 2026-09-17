// AppKernel regression (shared app foundation). The kernel owns only the loop
// + event pump + timing + exit policy; a headless server/CLI runs it with a
// null event source and null frame sink, proving presentation is not in the
// kernel.

#include "App/Kernel/AppKernel.h"
#include "Core/MessageBus.h"
#include "Core/Os/OsProcessLock.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace ya
{
namespace
{

uint32_t testProcessId()
{
#ifdef _WIN32
    return static_cast<uint32_t>(::_getpid());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

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

TEST(AppKernelTest, HeadlessLoopHonorsExitAfterTick)
{
    CountingDelegate delegate;
    AppKernel        kernel({}, delegate); // no event source, no frame sink
    const int        result = kernel.run(AppAutomationRunOptions{.exitAfterTick = 5});

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

// A deadline has to hold without anything else asking the app to stop: this is
// the guard against an unattended instance outliving whoever launched it.
TEST(AppKernelTest, HeadlessLoopHonorsMaxLifetime)
{
    CountingDelegate delegate;
    AppKernel        kernel({}, delegate);

    const int result = kernel.run(AppAutomationRunOptions{.maxLifetimeSeconds = 0.05});

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(delegate.shutdown);
    // No frame budget was given, so the loop can only have stopped on the clock.
    EXPECT_GT(delegate.ticks, 0);
}

// The lock tracks a live process, not a file on disk: a second holder is
// refused, and the key is usable again the moment the holder lets go, so an
// interrupted run never leaves the name permanently taken.
TEST(AppKernelTest, ProcessLockRefusesASecondHolderAndFreesOnRelease)
{
    const std::string key = "AppKernelTest.ProcessLockRefusesASecondHolder";

    Os::ProcessLock holder;
    uint32_t        ownerPid = 0;
    ASSERT_TRUE(holder.tryAcquire(key, ownerPid));
    EXPECT_TRUE(holder.isHeld());

    Os::ProcessLock contender;
    EXPECT_FALSE(contender.tryAcquire(key, ownerPid));
    EXPECT_FALSE(contender.isHeld());
    // The reported owner is best-effort, but must never be someone else here.
    if (ownerPid != 0) {
        EXPECT_EQ(ownerPid, testProcessId());
    }

    holder.release();
    EXPECT_TRUE(contender.tryAcquire(key, ownerPid));
    EXPECT_TRUE(contender.isHeld());
}

// The kernel is what makes the rule hold for every product line, so the refusal
// has to surface as a non-zero run() without ever ticking the app.
TEST(AppKernelTest, KernelRefusesToRunWhenKeyIsHeld)
{
    const std::string key = "AppKernelTest.KernelRefusesToRunWhenKeyIsHeld";

    uint32_t        ownerPid = 0;
    Os::ProcessLock holder;
    ASSERT_TRUE(holder.tryAcquire(key, ownerPid));

    CountingDelegate delegate;
    AppKernel        kernel({.instanceKey = key}, delegate);
    const int        result = kernel.run();

    EXPECT_NE(result, 0);
    EXPECT_EQ(delegate.ticks, 0);
    EXPECT_FALSE(delegate.started);
}

} // namespace ya
