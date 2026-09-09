#include "RHI/Core/CommandBuffer.h"
#include "RHI/Core/PresentFrame.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace ya
{
namespace
{

void recordPresentBarrier(ICommandBuffer& commandBuffer, VulkanSwapChain& swapchain, uint32_t imageIndex)
{
    const VkImageMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = swapchain.getVkImages()[imageIndex],
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer.getHandleAs<VkCommandBuffer>(),
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

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

bool presentOneFrame(IRender& render, IRenderSurfaceContext& surface, std::vector<std::shared_ptr<ICommandBuffer>>* cmds)
{
    int32_t imageIndex = -1;
    if (!surface.begin(&imageIndex)) {
        return false;
    }
    if (imageIndex < 0) {
        return true;
    }
    auto* swapchain = surface.getSwapchain()->as<VulkanSwapChain>();
    if (!swapchain) {
        return false;
    }
    if (cmds) {
        if (cmds->size() != swapchain->getImageCount()) {
            cmds->clear();
            render.allocateCommandBuffers(swapchain->getImageCount(), *cmds);
        }
        ICommandBuffer& cmd = *(*cmds)[static_cast<size_t>(imageIndex)];
        cmd.reset();
        cmd.begin(false);
        recordPresentBarrier(cmd, *swapchain, static_cast<uint32_t>(imageIndex));
        cmd.end();
        return surface.end(imageIndex, {cmd.getHandleAs<VkCommandBuffer>()});
    }
    return surface.end(imageIndex, {});
}

} // namespace

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
    EXPECT_EQ(primary->getSwapchain()->getImageCount(), render->primarySwapchain()->getImageCount());
    EXPECT_EQ(extra->getNativeWindow(), &extraWindow);
    EXPECT_EQ(render->primaryWindow(), &primaryWindow);

    std::vector<std::shared_ptr<ICommandBuffer>> primaryCmds;
    for (int frame = 0; frame < 3; ++frame) {
        ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));
        ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));
    }

    extra.reset();

    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

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

    std::vector<std::shared_ptr<ICommandBuffer>> primaryCmds;
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));
    ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));

    ASSERT_TRUE(extraWindow.setWindowSize(240, 180));
    auto* extraSwap = extra->getSwapchain()->as<VulkanSwapChain>();
    ASSERT_NE(extraSwap, nullptr);
    extraSwap->requestRecreate();

    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));
    ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));
    ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));

    extra.reset();
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

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

    std::vector<std::shared_ptr<ICommandBuffer>> primaryCmds;
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));
    ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));

    if (!extraWindow.minimize()) {
        GTEST_SKIP() << "native minimize is unavailable";
    }
    if (!extraWindow.isMinimized() && extra->isPresentable()) {
        GTEST_SKIP() << "platform did not mark extra window unpresentable after minimize";
    }

    auto* extraSwap = extra->getSwapchain()->as<VulkanSwapChain>();
    ASSERT_NE(extraSwap, nullptr);
    extraSwap->requestRecreate();

    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

    int32_t extraImage = -1;
    ASSERT_TRUE(extra->begin(&extraImage));
    EXPECT_LT(extraImage, 0);
    EXPECT_FALSE(extra->isPresentable());
    ASSERT_TRUE(extra->end(extraImage, {}));

    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

    ASSERT_TRUE(extraWindow.restoreFromMinimize());
    extraSwap->requestRecreate();
    ASSERT_TRUE(presentOneFrame(*render, *extra, nullptr));
    ASSERT_TRUE(extra->isPresentable());
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

    extra.reset();
    ASSERT_TRUE(presentOneFrame(*render, *primary, &primaryCmds));

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
