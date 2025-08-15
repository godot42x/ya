#include "VulkanBuffer.h"
#include "VulkanUtils.h"


#include "VulkanRender.h"



VulkanBuffer::~VulkanBuffer()
{
    if (_handle != VK_NULL_HANDLE) {
        VK_FREE(Memory, _render->getLogicalDevice(), _memory);
        VK_DESTROY(Buffer, _render->getLogicalDevice(), _handle);
    }
}

void VulkanBuffer::createInternal(void *data, uint32_t size)
{

    VkBuffer       stageBuffer       = nullptr;
    VkDeviceMemory stageBufferMemory = nullptr;

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           stageBuffer,
                           stageBufferMemory);

    void *mappedData = nullptr;
    VK_CALL(vkMapMemory(_render->getLogicalDevice(), stageBufferMemory, 0, size, 0, &mappedData));
    std::memcpy(mappedData, data, size);
    vkUnmapMemory(_render->getLogicalDevice(), stageBufferMemory);

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           _usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           _handle,
                           _memory);

    VulkanBuffer::transfer(_render, stageBuffer, _handle, size);

    VK_DESTROY(Buffer, _render->getLogicalDevice(), stageBuffer);
    VK_FREE(Memory, _render->getLogicalDevice(), stageBufferMemory);
}

bool VulkanBuffer::allocate(VulkanRender *render, uint32_t size,
                            VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage,
                            VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory)
{
    VkBufferCreateInfo bufferCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext       = nullptr,
        .flags       = 0,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        // TODO: why we need this?
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
    };

    VK_CALL(vkCreateBuffer(render->getLogicalDevice(), &bufferCreateInfo, nullptr, &outBuffer));


    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(render->getLogicalDevice(), outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = static_cast<uint32_t>(render->getMemoryIndex(memProperties, memRequirements.memoryTypeBits)),
    };

    VK_CALL(vkAllocateMemory(render->getLogicalDevice(), &allocInfo, nullptr, &outBufferMemory));
    VK_CALL(vkBindBufferMemory(render->getLogicalDevice(), outBuffer, outBufferMemory, 0));

    return true;
}

void VulkanBuffer::transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size)
{

    VkCommandBuffer cmdBuf = render->beginIsolateCommands();

    // VulkanUtils::transitionImageLayout(
    //     render->getLogicalDevice(),
    //     render->getGraphicsCommandPool(),
    //     srcBuffer,
    //     VK_IMAGE_LAYOUT_UNDEFINED,
    //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);


    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = size,
    };
    vkCmdCopyBuffer(cmdBuf, srcBuffer, dstBuffer, 1, &copyRegion);

    render->endIsolateCommands(cmdBuf);
}
