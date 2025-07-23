#pragma once


#include "Core/Base.h"
#include "VulkanUtils.h"

#include <vulkan/vulkan.h>

struct VulkanQueue
{
    uint32_t _familyIndex;
    uint32_t _index;
    VkQueue  _queue;
    bool     _canPresent;

  public:
    VulkanQueue(uint32_t familyIndex, uint32_t index, VkQueue queue, bool canPresent)
        : _familyIndex(familyIndex), _index(index), _queue(queue), _canPresent(canPresent)
    {
        NE_CORE_ASSERT(_queue != VK_NULL_HANDLE, "Vulkan queue is null!");
    }
    ~VulkanQueue() = default;

    void waitIdle()
    {
        VK_CALL(vkQueueWaitIdle(_queue));
    }
};
