#include "VulkanRenderSurfaceContext.h"

#include "RHI/Core/CommandBuffer.h"
#include "RHI/NativeWindow.h"
#include "VulkanCommandBuffer.h"
#include "VulkanRender.h"
#include "VulkanQueue.h"

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

    waitInFlight();

    _scratchPresentCmd.reset();
    releaseSyncResources();
    _swapChain.reset();

    if (_bOwnsSurface && _surface != VK_NULL_HANDLE && _window) {
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
    presentCompleteFences.resize(presentCompleteFenceCount);
    presentCompleteFenceIdx = 0;

    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        const VkResult ret = vkCreateSemaphore(_render->getDevice(), &semaphoreInfo, nullptr, &imageSubmittedSignalSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create {} render-finished semaphore", _debugLabel);
        _render->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                    imageSubmittedSignalSemaphores[i],
                                    std::format("{}_RenderFinishedSemaphore_{}", _debugLabel, i).c_str());
    }

    VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (uint32_t i = 0; i < flightFrameSize; ++i) {
        VkResult ret = vkCreateSemaphore(_render->getDevice(), &semaphoreInfo, nullptr, &frameImageAvailableSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create {} image-available semaphore", _debugLabel);
        ret = vkCreateFence(_render->getDevice(), &fenceInfo, nullptr, &frameFences[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create {} frame fence", _debugLabel);
        _render->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                    frameFences[i],
                                    std::format("{}_FrameFence_{}", _debugLabel, i).c_str());
        _render->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                    frameImageAvailableSemaphores[i],
                                    std::format("{}_ImageAvailableSemaphore_{}", _debugLabel, i).c_str());
    }

    for (uint32_t i = 0; i < presentCompleteFenceCount; ++i) {
        const VkResult ret = vkCreateFence(_render->getDevice(), &fenceInfo, nullptr, &presentCompleteFences[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create {} present-complete fence", _debugLabel);
        _render->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                    presentCompleteFences[i],
                                    std::format("{}_PresentCompleteFence_{}", _debugLabel, i).c_str());
    }
}

void VulkanRenderSurfaceContext::releaseSyncResources()
{
    if (!_render || _render->getDevice() == VK_NULL_HANDLE) {
        frameImageAvailableSemaphores.clear();
        frameFences.clear();
        presentCompleteFences.clear();
        imageSubmittedSignalSemaphores.clear();
        return;
    }

    for (VkSemaphore semaphore : frameImageAvailableSemaphores) {
        vkDestroySemaphore(_render->getDevice(), semaphore, _render->getAllocator());
    }
    for (VkFence fence : frameFences) {
        vkDestroyFence(_render->getDevice(), fence, _render->getAllocator());
    }
    for (VkFence fence : presentCompleteFences) {
        vkDestroyFence(_render->getDevice(), fence, _render->getAllocator());
    }
    for (VkSemaphore semaphore : imageSubmittedSignalSemaphores) {
        vkDestroySemaphore(_render->getDevice(), semaphore, _render->getAllocator());
    }
    frameImageAvailableSemaphores.clear();
    frameFences.clear();
    presentCompleteFences.clear();
    imageSubmittedSignalSemaphores.clear();
}

bool VulkanRenderSurfaceContext::createSwapchainAndSync(const SwapchainCreateInfo& swapchainCI, bool bRequireImages)
{
    _swapChain = std::make_unique<VulkanSwapChain>(_render, _surface, _window);
    if (!_swapChain->recreate(swapchainCI)) {
        YA_CORE_ERROR("{}: failed to create swapchain", _debugLabel);
        return false;
    }
    if (bRequireImages && _swapChain->getImageCount() == 0) {
        YA_CORE_ERROR("{}: swapchain has no images", _debugLabel);
        return false;
    }

    createSyncResources(_swapChain->getImageCount());
    return true;
}

bool VulkanRenderSurfaceContext::init(VulkanRender* render, INativeWindow& window, const SwapchainCreateInfo& swapchainCI)
{
    YA_CORE_ASSERT(render, "VulkanRenderSurfaceContext requires a device owner");
    _render        = render;
    _window        = &window;
    _bOwnsSurface      = true;
    _bDeviceFrameOwner = false;
    _debugLabel        = "Extra";

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

    return createSwapchainAndSync(swapchainCI, true);
}

bool VulkanRenderSurfaceContext::attachExistingSurface(VulkanRender*              render,
                                                       INativeWindow&             window,
                                                       VkSurfaceKHR               surface,
                                                       const SwapchainCreateInfo& swapchainCI)
{
    YA_CORE_ASSERT(render, "VulkanRenderSurfaceContext requires a device owner");
    YA_CORE_ASSERT(surface != VK_NULL_HANDLE, "Primary surface context requires an existing VkSurfaceKHR");
    _render       = render;
    _window       = &window;
    _surface      = surface;
    _bOwnsSurface      = false;
    _bDeviceFrameOwner = true;
    _debugLabel        = "Primary";
    return createSwapchainAndSync(swapchainCI, false);
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

void VulkanRenderSurfaceContext::waitAllGraphicsFences()
{
    if (frameFences.empty()) {
        return;
    }
    YA_PERF_SCOPE(perf::sample::vulkanWaitFence(), perf::metric::cpuTimeMs(), perf::domain::render());
    YA_PROFILE_SCOPE("vkWaitFence1");
    VK_CALL(vkWaitForFences(_render->getDevice(),
                            static_cast<uint32_t>(frameFences.size()),
                            frameFences.data(),
                            VK_TRUE,
                            kFenceTimeout));
}

void VulkanRenderSurfaceContext::waitAllPresentCompleteFences()
{
    if (presentCompleteFences.empty()) {
        return;
    }
    VK_CALL(vkWaitForFences(_render->getDevice(),
                            static_cast<uint32_t>(presentCompleteFences.size()),
                            presentCompleteFences.data(),
                            VK_TRUE,
                            kFenceTimeout));
}

void VulkanRenderSurfaceContext::waitInFlightFence()
{
    waitAllGraphicsFences();
}

void VulkanRenderSurfaceContext::waitInFlight()
{
    waitAllGraphicsFences();
    waitAllPresentCompleteFences();
}

void VulkanRenderSurfaceContext::resetInFlightFence()
{
    VK_CALL(vkResetFences(_render->getDevice(), 1, &frameFences[currentFrameIdx]));
}

void VulkanRenderSurfaceContext::signalPresentComplete()
{
    if (presentCompleteFences.empty() || _render->getPresentQueues().empty()) {
        return;
    }

    VkFence fence = presentCompleteFences[presentCompleteFenceIdx];
    VK_CALL(vkWaitForFences(_render->getDevice(), 1, &fence, VK_TRUE, kFenceTimeout));
    VK_CALL(vkResetFences(_render->getDevice(), 1, &fence));
    _render->getPresentQueues()[0].submit(
        std::vector<VkCommandBuffer>{},
        {},
        {},
        fence);
    presentCompleteFenceIdx = (presentCompleteFenceIdx + 1) % presentCompleteFenceCount;
}

void VulkanRenderSurfaceContext::resignalCurrentFence()
{
    if (frameFences.empty() || _render->getGraphicsQueues().empty()) {
        return;
    }
    _render->getGraphicsQueues()[0].submit(
        std::vector<VkCommandBuffer>{},
        {},
        {},
        frameFences[currentFrameIdx]);
}

bool VulkanRenderSurfaceContext::prepareSwapchainForAcquire()
{
    if (_swapChain->isRecreateDirty()) {
        waitAllPresentCompleteFences();
    }

    if (!_swapChain->flushDirtyRecreateAtFrameBegin()) {
        YA_CORE_ERROR("{}: failed to apply pending swapchain recreate", _debugLabel);
        return false;
    }

    if (_swapChain->getImageCount() != imageSubmittedSignalSemaphores.size() && _swapChain->getImageCount() > 0) {
        createSyncResources(_swapChain->getImageCount());
    }
    return true;
}

bool VulkanRenderSurfaceContext::acquire(int32_t* outImageIndex)
{
    YA_CORE_ASSERT(outImageIndex, "acquire requires an image index out-parameter");

    if (_swapChain->getImageSize() == 0) {
        *outImageIndex = -1;
        return true;
    }

    uint32_t imageIndex = 0;
    VkResult ret        = VK_SUCCESS;
    {
        YA_PERF_SCOPE(perf::sample::vulkanAcquire(), perf::metric::cpuTimeMs(), perf::domain::render());
        YA_PROFILE_SCOPE("acquireNextImage");
        ret = _swapChain->acquireNextImage(
            frameImageAvailableSemaphores[currentFrameIdx],
            frameFences[currentFrameIdx],
            imageIndex);
    }

    if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
        YA_PROFILE_SCOPE("VK_ERROR_OUT_OF_DATE_KHR");
        waitAllPresentCompleteFences();
        if (!_swapChain->recreate(_swapChain->getCreateInfo())) {
            YA_CORE_ERROR("{}: failed to recreate swapchain", _debugLabel);
            return false;
        }
        if (_swapChain->getImageCount() == 0) {
            resignalCurrentFence();
            *outImageIndex = -1;
            return true;
        }
        if (_swapChain->getImageCount() != imageSubmittedSignalSemaphores.size()) {
            createSyncResources(_swapChain->getImageCount());
        }
        VK_CALL(vkResetFences(_render->getDevice(), 1, &frameFences[currentFrameIdx]));
        {
            YA_PERF_SCOPE(perf::sample::vulkanAcquire(), perf::metric::cpuTimeMs(), perf::domain::render());
            ret = _swapChain->acquireNextImage(
                frameImageAvailableSemaphores[currentFrameIdx],
                frameFences[currentFrameIdx],
                imageIndex);
        }
        if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
            YA_CORE_ERROR("{}: failed to acquire after recreate: {}", _debugLabel, static_cast<int32_t>(ret));
            resignalCurrentFence();
            return false;
        }
        YA_CORE_ASSERT(imageIndex < _swapChain->getImageSize(),
                       "Invalid image index: {}. Swapchain image size: {}",
                       imageIndex,
                       _swapChain->getImageSize());
    }
    else if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
        YA_CORE_ERROR("{}: acquire failed: {}", _debugLabel, static_cast<int32_t>(ret));
        resignalCurrentFence();
        return false;
    }

    *outImageIndex = static_cast<int32_t>(imageIndex);
    return true;
}

bool VulkanRenderSurfaceContext::isPresentable() const
{
    if (!_window || _window->isMinimized()) {
        return false;
    }
    int width  = 0;
    int height = 0;
    _window->getWindowSize(width, height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    return _swapChain && _swapChain->isSurfacePresentable();
}

bool VulkanRenderSurfaceContext::begin(int32_t* outImageIndex)
{
    YA_PROFILE_FUNCTION();
    waitAllGraphicsFences();
    if (_bDeviceFrameOwner) {
        _render->onPrimaryPresentFenceWaited();
    }

    YA_CORE_ASSERT(outImageIndex, "begin requires an image index out-parameter");
    if (!isPresentable()) {
        *outImageIndex = -1;
        return true;
    }

    if (!prepareSwapchainForAcquire()) {
        return false;
    }
    if (!isPresentable() || _swapChain->getImageSize() == 0) {
        *outImageIndex = -1;
        return true;
    }
    resetInFlightFence();
    return acquire(outImageIndex);
}

bool VulkanRenderSurfaceContext::submitAndPresent(int32_t imageIndex, std::vector<void*> commandBuffers, bool bScratchIfEmpty)
{
    YA_PROFILE_FUNCTION();
    if (imageIndex < 0) {
        return true;
    }

    std::vector<void*> submits = std::move(commandBuffers);
    if (submits.empty() && bScratchIfEmpty) {
        if (!_scratchPresentCmd) {
            std::vector<std::shared_ptr<ICommandBuffer>> buffers;
            _render->allocateCommandBuffers(1, buffers);
            _scratchPresentCmd = buffers.empty() ? nullptr : buffers.front();
        }
        if (!_scratchPresentCmd) {
            YA_CORE_ERROR("{}: failed to allocate present command buffer", _debugLabel);
            return false;
        }
        _scratchPresentCmd->reset();
        _scratchPresentCmd->begin(false);
        recordPresentBarrier(_scratchPresentCmd->getHandleAs<VkCommandBuffer>(), static_cast<uint32_t>(imageIndex));
        _scratchPresentCmd->end();
        submits.push_back(_scratchPresentCmd->getHandleAs<VkCommandBuffer>());
    }

    if (!submits.empty()) {
        _render->submitToQueue(
            submits,
            {frameImageAvailableSemaphores[currentFrameIdx]},
            {imageSubmittedSignalSemaphores[static_cast<uint32_t>(imageIndex)]},
            frameFences[currentFrameIdx]);
    }

    int result = VK_SUCCESS;
    {
        YA_PERF_SCOPE(perf::sample::vulkanPresent(), perf::metric::cpuTimeMs(), perf::domain::render());
        result = _swapChain->presentImage(
            static_cast<uint32_t>(imageIndex),
            {imageSubmittedSignalSemaphores[static_cast<uint32_t>(imageIndex)]});
    }

    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
        _swapChain->requestRecreate();
    }
    else if (result != VK_SUCCESS) {
        YA_CORE_ERROR("{}: present failed: {}", _debugLabel, static_cast<int32_t>(result));
        signalPresentComplete();
        advanceFrame();
        return false;
    }

    signalPresentComplete();
    advanceFrame();
    return true;
}

bool VulkanRenderSurfaceContext::end(int32_t imageIndex, std::vector<void*> commandBuffers)
{
    return submitAndPresent(imageIndex, std::move(commandBuffers), true);
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
