#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/SurfaceId.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"

#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <vector>

namespace ya
{
namespace
{

bool createTestWindow(SDLNativeWindow& window, const char* title, uint32_t width, uint32_t height)
{
    if (!window.init()) {
        return false;
    }
    return window.recreate(WindowCreateInfo{
        .renderAPI = ERenderAPI::Vulkan,
        .title     = title,
        .width     = width,
        .height    = height,
    });
}

/// One frame that presents `surfaces`, spelled the way a real frame is: the
/// device opens the frame (waiting the previous frame's GPU work), each surface
/// acquires, each submits, each presents.
bool presentOneFrame(IRender& render, std::span<IRenderSurfaceContext*> surfaces)
{
    render.beginRecordedFrame();

    std::vector<int32_t> imageIndex(surfaces.size(), -1);
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (!surfaces[i]->begin(&imageIndex[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (imageIndex[i] >= 0 && !surfaces[i]->submit(imageIndex[i], {})) {
            return false;
        }
    }
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (imageIndex[i] >= 0 && !surfaces[i]->present(imageIndex[i])) {
            return false;
        }
    }
    return true;
}

bool presentOneFrame(IRender& render, IRenderSurfaceContext& surface)
{
    IRenderSurfaceContext* one[] = {&surface};
    return presentOneFrame(render, std::span<IRenderSurfaceContext*>(one));
}

/// One presentable size with readback enabled: what every case here presents.
SwapchainCreateInfo testSurfaceDesc(uint32_t width, uint32_t height)
{
    return SwapchainCreateInfo{
        .bEnableTransferSrc = true,
        .width              = width,
        .height             = height,
    };
}

/// A device created for exactly one window -- the startup surface case. The
/// window is handed to the device, and the case asks for its surface back by
/// that window rather than assuming a rank.
RenderCreateInfo testRenderCI(INativeWindow& startupWindow, uint32_t width, uint32_t height)
{
    return RenderCreateInfo{
        .renderAPI = ERenderAPI::Vulkan,
        .startupSurfaces = {
            StartupSurfaceDesc{
                .window      = &startupWindow,
                .swapchainCI = testSurfaceDesc(width, height),
            },
        },
    };
}

} // namespace

/// The device's frame generation is a fact about a FRAME, not about a window.
///
/// It used to live on the device's one surface: only that surface's `begin()`
/// advanced the counter, read back the GPU timestamps and flushed the
/// deferred-deletion queue (VulkanRenderSurfaceContext::_bDeviceFrameOwner).
/// That ranks one window above another in the frame loop -- the thing the
/// per-surface presentation rework removes -- and it makes a frame that
/// presents no window retire nothing.
TEST(RHISurfaceContext, FrameBookkeepingBelongsToTheFrameNotAWindow)
{
    SDLNativeWindow window;
    if (!createTestWindow(window, "MW-210-FrameBookkeeping", 160, 120)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(window, 160, 120);

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    // Advanceable without any surface taking part: that is the whole point.
    EXPECT_EQ(render->recordedFrameIndex(), 0u);
    EXPECT_EQ(render->framesInFlight(), kFramesInFlight);
    render->beginRecordedFrame();
    EXPECT_EQ(render->recordedFrameIndex(), 1u);
    render->beginRecordedFrame();
    EXPECT_EQ(render->recordedFrameIndex(), 2u);

    // Presenting a window advances the generation exactly once -- by opening the
    // frame, not by presenting. Two presents in one frame are one frame.
    auto* startup = render->findSurface(window);
    ASSERT_NE(startup, nullptr);
    const uint64_t beforeFrame = render->recordedFrameIndex();
    ASSERT_TRUE(presentOneFrame(*render, *startup));
    EXPECT_EQ(render->recordedFrameIndex(), beforeFrame + 1);

    // Submitting is what arms the fence, so a frame that submits nothing leaves
    // it signaled instead of leaving the next frame to hang on it. Two idle
    // frames still advance and still wait cleanly.
    render->beginRecordedFrame();
    render->beginRecordedFrame();
    EXPECT_EQ(render->recordedFrameIndex(), beforeFrame + 3);

    // And an offscreen-only frame -- submitted work, no window presented -- is a
    // legal frame: the fence is the device's, so it needs no surface to hang on.
    render->beginRecordedFrame();
    ASSERT_TRUE(render->submitFrame({}, {}, {}));
    render->beginRecordedFrame();
    EXPECT_EQ(render->recordedFrameIndex(), beforeFrame + 5);

    render->waitIdle();
    render->destroy();
    delete render;
}

/// One frame, several windows.
///
/// This is what the split is for: each window acquires its own image and
/// presents it, but they are one FRAME -- one generation advance, one fence that
/// the next frame waits. A frame that presented a window per generation would
/// make "how many frames ran" depend on how many windows are open.
TEST(RHISurfaceContext, OneFrameCanPresentSeveralWindows)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(startupWindow, "MW-213-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-213-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);
    IRender*               render   = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId extraId = render->createSurfaceContext(extraWindow, testSurfaceDesc(200, 150));
    ASSERT_TRUE(extraId.valid());
    IRenderSurfaceContext* startup = render->findSurface(startupWindow);
    IRenderSurfaceContext* extra   = render->findSurface(extraId);
    ASSERT_NE(startup, nullptr);
    ASSERT_NE(extra, nullptr);

    IRenderSurfaceContext* pair[] = {startup, extra};
    constexpr int          kFrames = 4;
    for (int frame = 0; frame < kFrames; ++frame) {
        ASSERT_TRUE(presentOneFrame(*render, std::span<IRenderSurfaceContext*>(pair)))
            << "frame " << frame;
    }

    // Two windows presented per frame, and the generation moved once per frame:
    // a window does not own "a frame", the frame does.
    EXPECT_EQ(render->recordedFrameIndex(), static_cast<uint64_t>(kFrames));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowAcquireSubmitPresentIndependentOfStartupWindow)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(startupWindow, "MW-201-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-201-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId extraId = render->createSurfaceContext(extraWindow, testSurfaceDesc(200, 150));
    ASSERT_TRUE(extraId.valid());
    IRenderSurfaceContext* extra = render->findSurface(extraId);
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(extra->getSwapchain(), nullptr);
    auto* startup = render->findSurface(startupWindow);
    ASSERT_NE(startup, nullptr);
    ASSERT_NE(startup->getSwapchain(), extra->getSwapchain());
    // A window supplies its own acquire/present sync. The frame's completion
    // fence is the device's now, which is why there is no per-window one left to
    // ask for.
    EXPECT_NE(startup->getCurrentImageAvailableSemaphore(), nullptr);
    EXPECT_NE(startup->getRenderFinishedSemaphore(0), nullptr);
    EXPECT_NE(startup->getSwapchain(), nullptr);
    EXPECT_EQ(extra->getNativeWindow(), &extraWindow);
    EXPECT_EQ(startup->getNativeWindow(), &startupWindow);
    // Identity answers "which surface", and it answers it for both windows:
    // the startup window is not the one the device "is", it is the one that
    // happens to have existed before the device did.
    EXPECT_EQ(render->findSurfaceId(startupWindow), render->findSurfaceId(startupWindow));
    EXPECT_NE(render->findSurfaceId(startupWindow), render->findSurfaceId(extraWindow));
    EXPECT_EQ(render->findSurface(render->findSurfaceId(extraWindow)), extra);

    for (int frame = 0; frame < 3; ++frame) {
        ASSERT_TRUE(presentOneFrame(*render, *startup));
        ASSERT_TRUE(presentOneFrame(*render, *extra));
    }

    ASSERT_TRUE(render->destroySurfaceContext(extraId));

    ASSERT_TRUE(presentOneFrame(*render, *startup));

    render->waitIdle();
    render->destroy();
    delete render;
}

/// A surface id names a registry slot AND which tenant of that slot it is.
///
/// Releasing a surface and registering another window of the same size is the
/// case that tells the two apart: the second registration takes the slot back,
/// and the id held for the first must resolve to nothing rather than quietly
/// naming the new window. That is what an app holding "the surface I present"
/// across a tear-off window closing and reopening depends on.
TEST(RHISurfaceContext, AReleasedSurfaceIdDoesNotResolveToTheNextTenantOfItsSlot)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow firstWindow;
    SDLNativeWindow secondWindow;
    if (!createTestWindow(startupWindow, "MW-211-A", 160, 120) ||
        !createTestWindow(firstWindow, "MW-211-B", 200, 150) ||
        !createTestWindow(secondWindow, "MW-211-C", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);
    IRender*               render   = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId firstId = render->createSurfaceContext(firstWindow, testSurfaceDesc(200, 150));
    ASSERT_TRUE(firstId.valid());
    EXPECT_EQ(render->findSurface(firstId)->getNativeWindow(), &firstWindow);

    ASSERT_TRUE(render->destroySurfaceContext(firstId));
    // Released: the id names nothing, and so does the released window.
    EXPECT_EQ(render->findSurface(firstId), nullptr);
    EXPECT_EQ(render->findSurface(firstWindow), nullptr);
    EXPECT_FALSE(render->findSurfaceId(firstWindow).valid());
    // A released id cannot act on the registry either.
    EXPECT_FALSE(render->destroySurfaceContext(firstId));

    const SurfaceId secondId = render->createSurfaceContext(secondWindow, testSurfaceDesc(200, 150));
    ASSERT_TRUE(secondId.valid());
    EXPECT_EQ(secondId.index, firstId.index) << "the freed slot should be reused";
    EXPECT_NE(secondId.generation, firstId.generation) << "a reused slot is a new tenant";
    EXPECT_EQ(render->findSurface(firstId), nullptr) << "the stale id must not name the new window";
    EXPECT_EQ(render->findSurface(secondId)->getNativeWindow(), &secondWindow);

    render->waitIdle();
    render->destroy();
    delete render;
}

/// A device is created for a set of windows, and the windows it was created for
/// are not privileged over the ones registered afterwards.
TEST(RHISurfaceContext, StartupWindowsAndLaterWindowsAreRegisteredAlike)
{
    SDLNativeWindow firstStartup;
    SDLNativeWindow secondStartup;
    if (!createTestWindow(firstStartup, "MW-212-A", 160, 120) ||
        !createTestWindow(secondStartup, "MW-212-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .startupSurfaces = {
            StartupSurfaceDesc{.window = &firstStartup, .swapchainCI = testSurfaceDesc(160, 120)},
            StartupSurfaceDesc{.window = &secondStartup, .swapchainCI = testSurfaceDesc(200, 150)},
        },
    };
    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    // Both startup windows present, and neither is answered by a rank: each is
    // found through its own window.
    IRenderSurfaceContext* first  = render->findSurface(firstStartup);
    IRenderSurfaceContext* second = render->findSurface(secondStartup);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first, second);
    EXPECT_NE(first->getSwapchain(), second->getSwapchain());
    EXPECT_TRUE(first->isPresentable());
    EXPECT_TRUE(second->isPresentable());
    EXPECT_TRUE(presentOneFrame(*render, *first));
    EXPECT_TRUE(presentOneFrame(*render, *second));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowResizeAndCloseDoesNotDeviceWaitIdleStartupWindow)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(startupWindow, "MW-202-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-202-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId        extraId = render->createSurfaceContext(extraWindow, testSurfaceDesc(200, 150));
    IRenderSurfaceContext* extra   = render->findSurface(extraId);
    ASSERT_NE(extra, nullptr);
    auto* startup = render->findSurface(startupWindow);
    ASSERT_NE(startup, nullptr);

    ASSERT_TRUE(presentOneFrame(*render, *startup));
    ASSERT_TRUE(presentOneFrame(*render, *extra));

    ASSERT_TRUE(extraWindow.setWindowSize(240, 180));
    extra->requestRecreate();

    ASSERT_TRUE(presentOneFrame(*render, *startup));
    ASSERT_TRUE(presentOneFrame(*render, *extra));
    ASSERT_TRUE(presentOneFrame(*render, *startup));
    ASSERT_TRUE(presentOneFrame(*render, *extra));

    ASSERT_TRUE(render->destroySurfaceContext(extraId));
    ASSERT_TRUE(presentOneFrame(*render, *startup));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowPresentResizeCloseSoak)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(startupWindow, "C9-soak-A", 160, 120) ||
        !createTestWindow(extraWindow, "C9-soak-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId        extraId = render->createSurfaceContext(extraWindow, testSurfaceDesc(200, 150));
    IRenderSurfaceContext* extra   = render->findSurface(extraId);
    ASSERT_NE(extra, nullptr);
    auto* startup = render->findSurface(startupWindow);
    ASSERT_NE(startup, nullptr);
    ASSERT_NE(startup, extra);

    constexpr int kFrames = 32;
    for (int frame = 0; frame < kFrames; ++frame) {
        ASSERT_TRUE(presentOneFrame(*render, *startup)) << "startup frame " << frame;
        ASSERT_TRUE(presentOneFrame(*render, *extra)) << "extra frame " << frame;
        if (frame == 8) {
            ASSERT_TRUE(extraWindow.setWindowSize(240, 180));
            extra->requestRecreate();
        }
        if (frame == 16) {
            ASSERT_TRUE(extraWindow.setWindowSize(160, 120));
            extra->requestRecreate();
        }
    }

    ASSERT_TRUE(render->destroySurfaceContext(extraId));
    for (int frame = 0; frame < 8; ++frame) {
        ASSERT_TRUE(presentOneFrame(*render, *startup)) << "startup after extra close " << frame;
    }

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowUnpresentableDoesNotBlockStartupWindowPresent)
{
    SDLNativeWindow startupWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(startupWindow, "MW-206-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-206-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    const RenderCreateInfo renderCI = testRenderCI(startupWindow, 160, 120);

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    const SurfaceId        extraId = render->createSurfaceContext(extraWindow, testSurfaceDesc(200, 150));
    IRenderSurfaceContext* extra   = render->findSurface(extraId);
    ASSERT_NE(extra, nullptr);
    auto* startup = render->findSurface(startupWindow);
    ASSERT_NE(startup, nullptr);
    EXPECT_TRUE(startup->isPresentable());
    EXPECT_TRUE(extra->isPresentable());

    ASSERT_TRUE(presentOneFrame(*render, *startup));
    ASSERT_TRUE(presentOneFrame(*render, *extra));

    // This guard needs the platform to actually drive the minimize/restore
    // transition, and it says so up front instead of asserting half of it: in
    // this harness SDL keeps (or keeps missing) SDL_WINDOW_MINIMIZED for a
    // window the window manager never really miniaturized, and a stale flag is
    // a reason the code under test cannot act on. Both directions are checked
    // so a red here always means the surface path, not the flag.
    if (!extraWindow.minimize()) {
        GTEST_SKIP() << "native minimize is unavailable";
    }
    if (!extraWindow.isMinimized()) {
        GTEST_SKIP() << "platform did not mark the extra window minimized";
    }

    extra->requestRecreate();

    ASSERT_TRUE(presentOneFrame(*render, *startup));

    int32_t extraImage = -1;
    ASSERT_TRUE(extra->begin(&extraImage));
    EXPECT_LT(extraImage, 0);
    EXPECT_FALSE(extra->isPresentable());
    // Unpresentable: submit and present are both no-ops for it, which is what
    // keeps one minimized window from stopping the other's frame.
    ASSERT_TRUE(extra->submit(extraImage, {}));
    ASSERT_TRUE(extra->present(extraImage));

    ASSERT_TRUE(presentOneFrame(*render, *startup));

    ASSERT_TRUE(extraWindow.restoreFromMinimize());
    extra->requestRecreate();
    ASSERT_TRUE(presentOneFrame(*render, *extra));
    if (extraWindow.isMinimized()) {
        GTEST_SKIP() << "platform kept the minimized flag after restore";
    }
    ASSERT_TRUE(extra->isPresentable());
    ASSERT_TRUE(presentOneFrame(*render, *startup));

    ASSERT_TRUE(render->destroySurfaceContext(extraId));
    ASSERT_TRUE(presentOneFrame(*render, *startup));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(PresentFrameTest, AcquiredRequiresSurfaceAndNonNegativeImage)
{
    FPresentFrame frame;
    EXPECT_FALSE(frame.acquired());
    frame.imageIndex = 0;
    EXPECT_FALSE(frame.acquired());
    frame.imageIndex = -1;
    EXPECT_FALSE(frame.acquired());
}

} // namespace ya
