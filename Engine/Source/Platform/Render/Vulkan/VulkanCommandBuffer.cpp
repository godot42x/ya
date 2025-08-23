#include "VulkanCommandBuffer.h"
#include "VulkanQueue.h"
#include "VulkanRender.h"


VulkanCommandPool::VulkanCommandPool(VulkanRender *render, VulkanQueue *queue, VkCommandPoolCreateFlags flags)
{
    _render = render;
    VkCommandPoolCreateInfo ci{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = flags,
        .queueFamilyIndex = queue->_familyIndex,
    };

    YA_CORE_ASSERT(vkCreateCommandPool(render->getLogicalDevice(), &ci, nullptr, &_handle) == VK_SUCCESS,
                   "Failed to create command pool!");
    YA_CORE_TRACE("Created command pool: {} success, queue family: {}", (uintptr_t)_handle, queue->_familyIndex);
}

bool VulkanCommandPool::allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer &outCommandBuffer)
{
    VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = _handle,
        .level              = level,
        .commandBufferCount = 1,
    };

    YA_CORE_ASSERT(vkAllocateCommandBuffers(_render->getLogicalDevice(), &allocInfo, &outCommandBuffer) == VK_SUCCESS,
                   "Failed to allocate command buffer!");
    YA_CORE_TRACE("Allocated command buffer success: {}", (uintptr_t)outCommandBuffer);

    return true;
}

void VulkanCommandPool::cleanup()
{
    VK_DESTROY(CommandPool, _render->getLogicalDevice(), _handle);
}
