#include "OffscreenTaskService.h"

#include "Runtime/Application/App.h"

#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{

void OffscreenTaskService::init(IRender* render)
{
    _render = render;
    if (!_render) {
        return;
    }

    std::vector<stdptr<ICommandBuffer>> cmdBufs;
    _render->allocateCommandBuffers(_render->getSwapchainImageCount() + 1, cmdBufs);
    YA_CORE_ASSERT(!cmdBufs.empty(), "Failed to allocate offscreen command buffer");
    _commandBuffer = cmdBufs.back();

    auto*             vkRender = static_cast<VulkanRender*>(_render);
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    VkFence  fence = VK_NULL_HANDLE;
    VkResult ret   = vkCreateFence(vkRender->getDevice(), &fenceCI, nullptr, &fence);
    YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create offscreen fence");
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_FENCE, fence, "OffscreenFence");
    _fence   = fence;
    _pending = false;
}

void OffscreenTaskService::shutdown()
{
    if (_pending && _fence && _render) {
        auto*   vkRender = static_cast<VulkanRender*>(_render);
        VkFence fence    = static_cast<VkFence>(_fence);
        vkWaitForFences(vkRender->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vkRender->getDevice(), 1, &fence);
        _pending = false;
    }

    if (!_submittedJobs.empty()) {
        finalizeCompletedJobs();
    }

    if (_fence && _render) {
        auto* vkRender = static_cast<VulkanRender*>(_render);
        vkDestroyFence(vkRender->getDevice(), static_cast<VkFence>(_fence), nullptr);
    }

    _submittedJobs.clear();
    _commandBuffer.reset();
    _fence   = nullptr;
    _pending = false;
    _render  = nullptr;
}

void OffscreenTaskService::tick(App& app)
{
    YA_PROFILE_FUNCTION()
    if (!_render || !_commandBuffer) {
        return;
    }

    if (_pending && _fence) {
        auto*   vkRender = static_cast<VulkanRender*>(_render);
        VkFence fence    = static_cast<VkFence>(_fence);
        vkWaitForFences(vkRender->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vkRender->getDevice(), 1, &fence);
        _pending = false;
        finalizeCompletedJobs();
    }

    if (!app.getTaskManager().hasOffscreenTasks()) {
        return;
    }

    auto cmdBuf = _commandBuffer;
    cmdBuf->reset();
    if (!cmdBuf->begin()) {
        YA_CORE_ERROR("Failed to begin offscreen command buffer");
        return;
    }

    _submittedJobs.clear();
    app.getTaskManager().updateOffscreenTasks(cmdBuf.get(), &_submittedJobs);

    if (!cmdBuf->end()) {
        YA_CORE_ERROR("Failed to end offscreen command buffer");
        return;
    }

    _render->submitToQueue({cmdBuf->getHandle()}, {}, {}, _fence);
    _pending = true;
}

void OffscreenTaskService::finalizeCompletedJobs()
{
    ya::finalizeSubmittedOffscreenJobs(_submittedJobs);
}

} // namespace ya
