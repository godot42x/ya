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
        YA_CORE_ASSERT(_handle != VK_NULL_HANDLE, "Vulkan queue is null!");
    }
    ~VulkanQueue() = default;

    void waitIdle()
    {
        VK_CALL(vkQueueWaitIdle(_handle));
    }

    void submit(const std::vector<VkCommandBuffer> &commandBuffers,
                const std::vector<VkSemaphore>     &waitSemaphores   = {}, // waiting
                const std::vector<VkSemaphore>     &signalSemaphores = {}, // trigger/signal semaphore after submission completed
                VkFence                             emitFence        = VK_NULL_HANDLE)
    {
        submit((void *)commandBuffers.data(),
               static_cast<uint32_t>(commandBuffers.size()),
               waitSemaphores,
               signalSemaphores,
               emitFence);
    }

    void submit(const void *commandBuffers, uint32_t size,
                const std::vector<VkSemaphore> &waitSemaphores   = {}, // waiting
                const std::vector<VkSemaphore> &signalSemaphores = {}, // trigger/signal semaphore after submission completed
                VkFence                         emitFence        = VK_NULL_HANDLE)
    {
        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         info{
                    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext                = nullptr,
                    .waitSemaphoreCount   = static_cast<uint32_t>(waitSemaphores.size()),
                    .pWaitSemaphores      = waitSemaphores.data(),
                    .pWaitDstStageMask    = &waitStageMask,
                    .commandBufferCount   = size,
                    .pCommandBuffers      = static_cast<const VkCommandBuffer *>(commandBuffers),
                    .signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size()),
                    .pSignalSemaphores    = signalSemaphores.data(),
        };

        VK_CALL(vkQueueSubmit(_handle, 1, &info, emitFence));
    }

    [[nodiscard]] VkQueue  getHandle() const { return _handle; }
    [[nodiscard]] uint32_t getFamilyIndex() const { return _familyIndex; }
};
