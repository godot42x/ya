#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"

#include <gtest/gtest.h>
#include <memory>

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

bool presentOneFrame(IRenderSurfaceContext& surface)
{
    int32_t imageIndex = -1;
    if (!surface.begin(&imageIndex)) {
        return false;
    }
    if (imageIndex < 0) {
        return true;
    }
    return surface.end(imageIndex, {});
}

} // namespace

/// The device's frame generation is a fact about a FRAME, not about a window.
///
/// It used to live on the primary surface: only that surface's `begin()`
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

    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .bEnableTransferSrc = true,
            .width              = 160,
            .height             = 120,
        },
        .nativeWindow = &window,
    };

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

    // And presenting a window does NOT advance it: no surface owns the frame.
    auto* primary = render->getPrimarySurfaceContext();
    ASSERT_NE(primary, nullptr);
    const uint64_t beforePresent = render->recordedFrameIndex();
    ASSERT_TRUE(presentOneFrame(*primary));
    EXPECT_EQ(render->recordedFrameIndex(), beforePresent);

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowAcquireSubmitPresentIndependentOfPrimary)
{
    SDLNativeWindow primaryWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(primaryWindow, "MW-201-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-201-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .bEnableTransferSrc = true,
            .width              = 160,
            .height             = 120,
        },
        .nativeWindow = &primaryWindow,
    };

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    std::unique_ptr<IRenderSurfaceContext> extra = render->createSurfaceContext(extraWindow);
    ASSERT_NE(extra, nullptr);
    ASSERT_NE(extra->getSwapchain(), nullptr);
    auto* primary = render->getPrimarySurfaceContext();
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(primary->getSwapchain(), extra->getSwapchain());
    EXPECT_NE(primary->getCurrentFrameFence(), nullptr);
    EXPECT_NE(primary->getCurrentImageAvailableSemaphore(), nullptr);
    // The primary surface is the one `getPrimarySurfaceContext()` names -- a
    // bootstrap fact, not a rank: there is no "the" swapchain accessor on the
    // device any more, so a caller holding a surface asks it directly.
    EXPECT_NE(primary->getSwapchain(), nullptr);
    EXPECT_EQ(extra->getNativeWindow(), &extraWindow);
    EXPECT_EQ(render->primaryWindow(), &primaryWindow);

    for (int frame = 0; frame < 3; ++frame) {
        ASSERT_TRUE(presentOneFrame(*primary));
        ASSERT_TRUE(presentOneFrame(*extra));
    }

    extra.reset();

    ASSERT_TRUE(presentOneFrame(*primary));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowResizeAndCloseDoesNotDeviceWaitIdlePrimary)
{
    SDLNativeWindow primaryWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(primaryWindow, "MW-202-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-202-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .bEnableTransferSrc = true,
            .width              = 160,
            .height             = 120,
        },
        .nativeWindow = &primaryWindow,
    };

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    std::unique_ptr<IRenderSurfaceContext> extra = render->createSurfaceContext(extraWindow);
    ASSERT_NE(extra, nullptr);
    auto* primary = render->getPrimarySurfaceContext();
    ASSERT_NE(primary, nullptr);

    ASSERT_TRUE(presentOneFrame(*primary));
    ASSERT_TRUE(presentOneFrame(*extra));

    ASSERT_TRUE(extraWindow.setWindowSize(240, 180));
    extra->requestRecreate();

    ASSERT_TRUE(presentOneFrame(*primary));
    ASSERT_TRUE(presentOneFrame(*extra));
    ASSERT_TRUE(presentOneFrame(*primary));
    ASSERT_TRUE(presentOneFrame(*extra));

    extra.reset();
    ASSERT_TRUE(presentOneFrame(*primary));

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowPresentResizeCloseSoak)
{
    SDLNativeWindow primaryWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(primaryWindow, "C9-soak-A", 160, 120) ||
        !createTestWindow(extraWindow, "C9-soak-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .bEnableTransferSrc = true,
            .width              = 160,
            .height             = 120,
        },
        .nativeWindow = &primaryWindow,
    };

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    std::unique_ptr<IRenderSurfaceContext> extra = render->createSurfaceContext(extraWindow);
    ASSERT_NE(extra, nullptr);
    auto* primary = render->getPrimarySurfaceContext();
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(primary, extra.get());

    constexpr int kFrames = 32;
    for (int frame = 0; frame < kFrames; ++frame) {
        ASSERT_TRUE(presentOneFrame(*primary)) << "primary frame " << frame;
        ASSERT_TRUE(presentOneFrame(*extra)) << "extra frame " << frame;
        if (frame == 8) {
            ASSERT_TRUE(extraWindow.setWindowSize(240, 180));
            extra->requestRecreate();
        }
        if (frame == 16) {
            ASSERT_TRUE(extraWindow.setWindowSize(160, 120));
            extra->requestRecreate();
        }
    }

    extra.reset();
    for (int frame = 0; frame < 8; ++frame) {
        ASSERT_TRUE(presentOneFrame(*primary)) << "primary after extra close " << frame;
    }

    render->waitIdle();
    render->destroy();
    delete render;
}

TEST(RHISurfaceContext, ExtraWindowUnpresentableDoesNotBlockPrimaryPresent)
{
    SDLNativeWindow primaryWindow;
    SDLNativeWindow extraWindow;
    if (!createTestWindow(primaryWindow, "MW-206-A", 160, 120) ||
        !createTestWindow(extraWindow, "MW-206-B", 200, 150)) {
        GTEST_SKIP() << "SDL native window create failed";
    }

    RenderCreateInfo renderCI{
        .renderAPI = ERenderAPI::Vulkan,
        .swapchainCI = SwapchainCreateInfo{
            .bEnableTransferSrc = true,
            .width              = 160,
            .height             = 120,
        },
        .nativeWindow = &primaryWindow,
    };

    IRender* render = IRender::create(renderCI);
    ASSERT_NE(render, nullptr);
    ASSERT_TRUE(render->init(renderCI));

    std::unique_ptr<IRenderSurfaceContext> extra = render->createSurfaceContext(extraWindow);
    ASSERT_NE(extra, nullptr);
    auto* primary = render->getPrimarySurfaceContext();
    ASSERT_NE(primary, nullptr);
    EXPECT_TRUE(primary->isPresentable());
    EXPECT_TRUE(extra->isPresentable());

    ASSERT_TRUE(presentOneFrame(*primary));
    ASSERT_TRUE(presentOneFrame(*extra));

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

    ASSERT_TRUE(presentOneFrame(*primary));

    int32_t extraImage = -1;
    ASSERT_TRUE(extra->begin(&extraImage));
    EXPECT_LT(extraImage, 0);
    EXPECT_FALSE(extra->isPresentable());
    ASSERT_TRUE(extra->end(extraImage, {}));

    ASSERT_TRUE(presentOneFrame(*primary));

    ASSERT_TRUE(extraWindow.restoreFromMinimize());
    extra->requestRecreate();
    ASSERT_TRUE(presentOneFrame(*extra));
    if (extraWindow.isMinimized()) {
        GTEST_SKIP() << "platform kept the minimized flag after restore";
    }
    ASSERT_TRUE(extra->isPresentable());
    ASSERT_TRUE(presentOneFrame(*primary));

    extra.reset();
    ASSERT_TRUE(presentOneFrame(*primary));

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
