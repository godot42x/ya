#pragma once


#include "Core/Base.h"
#include "VulkanUtils.h"

#include <vulkan/vulkan.h>

struct VulkanQueue
{
    uint32_t _familyIndex;
    uint32_t _index;
    VkQueue  _handle;
    bool     _canPresent;

  public:
    VulkanQueue(uint32_t familyIndex, uint32_t index, VkQueue queue, bool canPresent)
        : _familyIndex(familyIndex), _index(index), _handle(queue), _canPresent(canPresent)
    {
        NE_CORE_ASSERT(_handle != VK_NULL_HANDLE, "Vulkan queue is null!");
    }
    ~VulkanQueue() = default;

    void waitIdle()
    {
        VK_CALL(vkQueueWaitIdle(_handle));
    }
    void submit(std::vector<VkCommandBuffer> commandBuffers)
    {
        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo info{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 0,
            .pWaitSemaphores      = nullptr,
            .pWaitDstStageMask    = &waitStageMask,
            .commandBufferCount   = static_cast<uint32_t>(commandBuffers.size()),
            .pCommandBuffers      = commandBuffers.data(),
            .signalSemaphoreCount = 0,
            .pSignalSemaphores    = nullptr,
        };

        VK_CALL(vkQueueSubmit(_handle, 1, &info, nullptr));
    }

    [[nodiscard]] VkQueue getHandle() const { return _handle; }
};
