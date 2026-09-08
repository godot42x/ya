#include "RHI/Core/CommandBuffer.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanSwapChain.h"

#include <gtest/gtest.h>
#include <memory>
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
    ASSERT_NE(render->getSwapchain(), extra->getSwapchain());
    EXPECT_EQ(extra->getNativeWindow(), &extraWindow);
    EXPECT_EQ(render->getNativeWindow(), &primaryWindow);

    auto* primarySwap = render->getSwapchain()->as<VulkanSwapChain>();
    ASSERT_NE(primarySwap, nullptr);

    std::vector<std::shared_ptr<ICommandBuffer>> primaryCmds;
    render->allocateCommandBuffers(render->getSwapchainImageCount(), primaryCmds);
    ASSERT_EQ(primaryCmds.size(), render->getSwapchainImageCount());

    for (int frame = 0; frame < 3; ++frame) {
        int32_t primaryImage = -1;
        ASSERT_TRUE(render->begin(&primaryImage));
        if (primaryImage >= 0) {
            ICommandBuffer& cmd = *primaryCmds[static_cast<size_t>(primaryImage)];
            cmd.reset();
            cmd.begin(false);
            recordPresentBarrier(cmd, *primarySwap, static_cast<uint32_t>(primaryImage));
            cmd.end();
            ASSERT_TRUE(render->end(primaryImage, {cmd.getHandleAs<VkCommandBuffer>()}));
        }

        int32_t extraImage = -1;
        ASSERT_TRUE(extra->begin(&extraImage));
        if (extraImage >= 0) {
            ASSERT_TRUE(extra->end(extraImage, {}));
        }
    }

    extra.reset();
    render->waitIdle();

    int32_t primaryImage = -1;
    ASSERT_TRUE(render->begin(&primaryImage));
    if (primaryImage >= 0) {
        ICommandBuffer& cmd = *primaryCmds[static_cast<size_t>(primaryImage)];
        cmd.reset();
        cmd.begin(false);
        recordPresentBarrier(cmd, *primarySwap, static_cast<uint32_t>(primaryImage));
        cmd.end();
        ASSERT_TRUE(render->end(primaryImage, {cmd.getHandleAs<VkCommandBuffer>()}));
    }

    render->waitIdle();
    render->destroy();
    delete render;
}

} // namespace ya
