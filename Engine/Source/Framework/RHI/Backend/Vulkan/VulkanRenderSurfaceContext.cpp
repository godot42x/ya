#include "VulkanRenderSurfaceContext.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/NativeWindow.h"
#include "VulkanCommandBuffer.h"
#include "VulkanRender.h"

#include "Core/Log.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "VulkanUtils.h"

#include <format>

namespace ya
{

namespace
{

constexpr uint64_t kFenceTimeout = UINT64_MAX;

} // namespace

VulkanRenderSurfaceContext::~VulkanRenderSurfaceContext()
{
    if (!_render || _render->getDevice() == VK_NULL_HANDLE) {
        _scratchPresentCmd.reset();
        _swapChain.reset();
        return;
    }

    if (!frameFences.empty()) {
        vkWaitForFences(_render->getDevice(),
                        static_cast<uint32_t>(frameFences.size()),
                        frameFences.data(),
                        VK_TRUE,
                        kFenceTimeout);
    }
    // Present can still hold render-finished semaphores after the graphics
    // fence signals. Drain this device's queues before destroying sync.
    if (!_render->getGraphicsQueues().empty()) {
        _render->getGraphicsQueues()[0].waitIdle();
    }
    if (!_render->getPresentQueues().empty()) {
        _render->getPresentQueues()[0].waitIdle();
    }

    _scratchPresentCmd.reset();
    releaseSyncResources();
    _swapChain.reset();

    if (_surface != VK_NULL_HANDLE && _window) {
        _window->onDestroyVkSurface(_render->getInstance(), &_surface);
        _surface = VK_NULL_HANDLE;
    }
}

bool VulkanRenderSurfaceContext::queryPresentSupport() const
{
    VkBool32 presentSupported = VK_FALSE;
    const uint32_t presentFamily = static_cast<uint32_t>(_render->getPresentQueueFamilyInfo().queueFamilyIndex);
    vkGetPhysicalDeviceSurfaceSupportKHR(
        _render->getPhysicalDevice(),
        presentFamily,
        _surface,
        &presentSupported);
    if (presentSupported) {
        return true;
    }

    const uint32_t graphicsFamily = static_cast<uint32_t>(_render->getGraphicsQueueFamilyInfo().queueFamilyIndex);
    if (graphicsFamily == presentFamily) {
        return false;
    }
    vkGetPhysicalDeviceSurfaceSupportKHR(
        _render->getPhysicalDevice(),
        graphicsFamily,
        _surface,
        &presentSupported);
    return presentSupported == VK_TRUE;
}

void VulkanRenderSurfaceContext::createSyncResources(uint32_t swapchainImageCount)
{
    releaseSyncResources();

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    imageSubmittedSignalSemaphores.resize(swapchainImageCount);
    frameImageAvailableSemaphores.resize(flightFrameSize);
    frameFences.resize(flightFrameSize);

    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        const VkResult ret = vkCreateSemaphore(_render->getDevice(), &semaphoreInfo, nullptr, &imageSubmittedSignalSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create extra-surface render-finished semaphore");
        _render->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                    imageSubmittedSignalSemaphores[i],
                                    std::format("ExtraRenderFinishedSemaphore_{}", i).c_str());
    }

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (uint32_t i = 0; i < flightFrameSize; ++i) {
        VkResult ret = vkCreateSemaphore(_render->getDevice(), &semaphoreInfo, nullptr, &frameImageAvailableSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create extra-surface image-available semaphore");
        ret = vkCreateFence(_render->getDevice(), &fenceInfo, nullptr, &frameFences[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create extra-surface frame fence");
        _render->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                    frameFences[i],
                                    std::format("ExtraFrameFence_{}", i).c_str());
        _render->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                    frameImageAvailableSemaphores[i],
                                    std::format("ExtraImageAvailableSemaphore_{}", i).c_str());
    }
}

void VulkanRenderSurfaceContext::releaseSyncResources()
{
    if (!_render || _render->getDevice() == VK_NULL_HANDLE) {
        frameImageAvailableSemaphores.clear();
        frameFences.clear();
        imageSubmittedSignalSemaphores.clear();
        return;
    }

    for (VkSemaphore semaphore : frameImageAvailableSemaphores) {
        vkDestroySemaphore(_render->getDevice(), semaphore, _render->getAllocator());
    }
    for (VkFence fence : frameFences) {
        vkDestroyFence(_render->getDevice(), fence, _render->getAllocator());
    }
    for (VkSemaphore semaphore : imageSubmittedSignalSemaphores) {
        vkDestroySemaphore(_render->getDevice(), semaphore, _render->getAllocator());
    }
    frameImageAvailableSemaphores.clear();
    frameFences.clear();
    imageSubmittedSignalSemaphores.clear();
}

bool VulkanRenderSurfaceContext::init(VulkanRender* render, INativeWindow& window, const SwapchainCreateInfo& swapchainCI)
{
    YA_CORE_ASSERT(render, "VulkanRenderSurfaceContext requires a device owner");
    _render = render;
    _window = &window;

    if (!window.onCreateVkSurface(render->getInstance(), &_surface) || _surface == VK_NULL_HANDLE) {
        YA_CORE_ERROR("VulkanRenderSurfaceContext: failed to create VkSurfaceKHR");
        return false;
    }
    if (!queryPresentSupport()) {
        YA_CORE_ERROR("VulkanRenderSurfaceContext: physical device cannot present to this surface");
        window.onDestroyVkSurface(render->getInstance(), &_surface);
        _surface = VK_NULL_HANDLE;
        return false;
    }

    _swapChain = std::make_unique<VulkanSwapChain>(render, _surface, &window);
    if (!_swapChain->recreate(swapchainCI)) {
        YA_CORE_ERROR("VulkanRenderSurfaceContext: failed to create swapchain");
        return false;
    }
    if (_swapChain->getImageCount() == 0) {
        YA_CORE_ERROR("VulkanRenderSurfaceContext: swapchain has no images");
        return false;
    }

    createSyncResources(_swapChain->getImageCount());
    return true;
}

void VulkanRenderSurfaceContext::recordPresentBarrier(VkCommandBuffer commandBuffer, uint32_t imageIndex) const
{
    const VkImage image = _swapChain->getVkImages()[imageIndex];
    const VkImageMemoryBarrier barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = nullptr,
        .srcAccessMask       = 0,
        .dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(
        commandBuffer,
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

bool VulkanRenderSurfaceContext::begin(int32_t* outImageIndex)
{
    YA_PROFILE_FUNCTION();
    YA_CORE_ASSERT(outImageIndex, "begin requires an image index out-parameter");

    VK_CALL(vkWaitForFences(_render->getDevice(), 1, &frameFences[currentFrameIdx], VK_TRUE, kFenceTimeout));
    VK_CALL(vkResetFences(_render->getDevice(), 1, &frameFences[currentFrameIdx]));

    if (!_swapChain->flushDirtyRecreateAtFrameBegin()) {
        YA_CORE_ERROR("Extra surface: failed to apply pending swapchain recreate");
        return false;
    }

    if (_swapChain->getImageCount() != imageSubmittedSignalSemaphores.size() && _swapChain->getImageCount() > 0) {
        createSyncResources(_swapChain->getImageCount());
    }

    if (_swapChain->getImageSize() == 0) {
        *outImageIndex = -1;
        return true;
    }

    uint32_t imageIndex = 0;
    VkResult ret        = VK_SUCCESS;
    {
        YA_PERF_SCOPE(perf::sample::vulkanAcquire(), perf::metric::cpuTimeMs(), perf::domain::render());
        ret = _swapChain->acquireNextImage(
            frameImageAvailableSemaphores[currentFrameIdx],
            frameFences[currentFrameIdx],
            imageIndex);
    }

    if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
        VK_CALL(vkWaitForFences(_render->getDevice(),
                                static_cast<uint32_t>(frameFences.size()),
                                frameFences.data(),
                                VK_TRUE,
                                kFenceTimeout));
        if (!_swapChain->recreate(_swapChain->getCreateInfo())) {
            YA_CORE_ERROR("Extra surface: failed to recreate swapchain");
            return false;
        }
        if (_swapChain->getImageCount() == 0) {
            *outImageIndex = -1;
            return true;
        }
        if (_swapChain->getImageCount() != imageSubmittedSignalSemaphores.size()) {
            createSyncResources(_swapChain->getImageCount());
        }
        ret = _swapChain->acquireNextImage(
            frameImageAvailableSemaphores[currentFrameIdx],
            frameFences[currentFrameIdx],
            imageIndex);
        if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
            YA_CORE_ERROR("Extra surface: failed to acquire after recreate: {}", static_cast<int32_t>(ret));
            return false;
        }
    }
    else if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
        YA_CORE_ERROR("Extra surface: acquire failed: {}", static_cast<int32_t>(ret));
        return false;
    }

    *outImageIndex = static_cast<int32_t>(imageIndex);
    return true;
}

bool VulkanRenderSurfaceContext::end(int32_t imageIndex, std::vector<void*> commandBuffers)
{
    YA_PROFILE_FUNCTION();
    if (imageIndex < 0) {
        return true;
    }

    std::vector<void*> submits = std::move(commandBuffers);
    if (submits.empty()) {
        if (!_scratchPresentCmd) {
            std::vector<std::shared_ptr<ICommandBuffer>> buffers;
            _render->allocateCommandBuffers(1, buffers);
            _scratchPresentCmd = buffers.empty() ? nullptr : buffers.front();
        }
        if (!_scratchPresentCmd) {
            YA_CORE_ERROR("Extra surface: failed to allocate present command buffer");
            return false;
        }
        _scratchPresentCmd->reset();
        _scratchPresentCmd->begin(false);
        recordPresentBarrier(_scratchPresentCmd->getHandleAs<VkCommandBuffer>(), static_cast<uint32_t>(imageIndex));
        _scratchPresentCmd->end();
        submits.push_back(_scratchPresentCmd->getHandleAs<VkCommandBuffer>());
    }

    _render->submitToQueue(
        submits,
        {frameImageAvailableSemaphores[currentFrameIdx]},
        {imageSubmittedSignalSemaphores[static_cast<uint32_t>(imageIndex)]},
        frameFences[currentFrameIdx]);

    const VkResult result = _swapChain->presentImage(
        static_cast<uint32_t>(imageIndex),
        {imageSubmittedSignalSemaphores[static_cast<uint32_t>(imageIndex)]});
    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
        _swapChain->requestRecreate();
    }
    else if (result != VK_SUCCESS) {
        YA_CORE_ERROR("Extra surface: present failed: {}", static_cast<int32_t>(result));
        return false;
    }

    currentFrameIdx = (currentFrameIdx + 1) % flightFrameSize;
    return true;
}

void* VulkanRenderSurfaceContext::getCurrentImageAvailableSemaphore()
{
    return frameImageAvailableSemaphores[currentFrameIdx];
}

void* VulkanRenderSurfaceContext::getCurrentFrameFence()
{
    return frameFences[currentFrameIdx];
}

void* VulkanRenderSurfaceContext::getRenderFinishedSemaphore(uint32_t imageIndex)
{
    return imageSubmittedSignalSemaphores[imageIndex];
}

} // namespace ya
