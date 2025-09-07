#include "VulkanBuffer.h"
#include "VulkanUtils.h"


#include "VulkanRender.h"



VulkanBuffer::~VulkanBuffer()
{
    VK_FREE(Memory, _render->getDevice(), _memory);
    VK_DESTROY(Buffer, _render->getDevice(), _handle);
}

void VulkanBuffer::createWithDataInternal(const void *data, uint32_t size, VkMemoryPropertyFlags memProperties)
{

    VkBuffer       stageBuffer       = nullptr;
    VkDeviceMemory stageBufferMemory = nullptr;

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           memProperties | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           stageBuffer,
                           stageBufferMemory);

    void *mappedData = nullptr;
    VK_CALL(vkMapMemory(_render->getDevice(), stageBufferMemory, 0, size, 0, &mappedData));
    std::memcpy(mappedData, data, size);
    vkUnmapMemory(_render->getDevice(), stageBufferMemory);

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           _usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           _handle,
                           _memory);

    VulkanBuffer::transfer(_render, stageBuffer, _handle, size);

    VK_DESTROY(Buffer, _render->getDevice(), stageBuffer);
    VK_FREE(Memory, _render->getDevice(), stageBufferMemory);

    YA_CORE_TRACE("Created VulkanBuffer {}: {} of size: {} with usage: {}", name, (uintptr_t)_handle, _size, std::to_string(_usageFlags));
}

void VulkanBuffer::createDefaultInternal(uint32_t size, VkMemoryPropertyFlags memProperties)
{
    VulkanBuffer::allocate(_render,
                           size,
                           memProperties,
                           _usageFlags,
                           _handle,
                           _memory);

    YA_CORE_TRACE("Created VulkanBuffer {}: {} of size: {} with usage: {}", name, (uintptr_t)_handle, _size, std::to_string(_usageFlags));
}

bool VulkanBuffer::writeData(const void *data, uint32_t size, uint32_t offset)
{
    if (!data) {
        YA_CORE_ERROR("Write data to buffer {} failed: data is nullptr", name);
        return false;
    }
    if (!bHostVisible) {
        YA_CORE_ERROR("Write data to buffer {} failed: buffer is not host visible", name);
        return false;
    }
    YA_CORE_ASSERT(offset + size <= _size, "Write data out of range!");
    void *mappedData = nullptr;
    if (size == 0) {
        YA_CORE_ASSERT(offset == 0, "If size is 0, offset must be 0");
        VK_CALL(vkMapMemory(_render->getDevice(),
                            _memory,
                            0,
                            VK_WHOLE_SIZE,
                            0,
                            &mappedData));
    }
    else {

        VK_CALL(vkMapMemory(_render->getDevice(),
                            _memory,
                            offset,
                            size, // map the whole memory
                            0,
                            &mappedData));
    }
    std::memcpy(mappedData, data, size);
    vkUnmapMemory(_render->getDevice(), _memory);
    return true;
}

bool VulkanBuffer::flush(uint32_t size, uint32_t offset)
{
    YA_CORE_ASSERT(bHostVisible, "Buffer is not host visible, cannot flush!");

    VkMappedMemoryRange range{
        .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext  = nullptr,
        .memory = _memory,
        .offset = offset,
        .size   = size == 0 ? VK_WHOLE_SIZE : size,
    };
    VK_CALL(vkFlushMappedMemoryRanges(_render->getDevice(), 1, &range));
    return true;
}


void VulkanBuffer::mapInternal(void **ptr)
{
    YA_CORE_ASSERT(bHostVisible, "Buffer is not host visible, cannot map!");
    VK_CALL(vkMapMemory(_render->getDevice(),
                        _memory,
                        0,
                        VK_WHOLE_SIZE,
                        0,
                        ptr));
}

void VulkanBuffer::setupDebugName(const std::string &name)
{
    if (!name.empty()) {
        _render->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, _handle, name.c_str());
        _render->setDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, _memory, std::format("{}_Memory", name).c_str());
    }
}

void VulkanBuffer::unmap()
{
    vkUnmapMemory(_render->getDevice(), _memory);
}

bool VulkanBuffer::allocate(VulkanRender *render, uint32_t size, VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage, VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory)
{
    VkBufferCreateInfo vkBufferCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size  = size,
        .usage = usage,
        // .sharingMode = render->isGraphicsPresentSameQueueFamily() ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        // TODO: why we need this?
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
    };

    VK_CALL(vkCreateBuffer(render->getDevice(), &vkBufferCI, nullptr, &outBuffer));


    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(render->getDevice(), outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = nullptr,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = static_cast<uint32_t>(render->getMemoryIndex(memProperties, memRequirements.memoryTypeBits)),
    };

    VK_CALL(vkAllocateMemory(render->getDevice(), &allocInfo, nullptr, &outBufferMemory));
    VK_CALL(vkBindBufferMemory(render->getDevice(), outBuffer, outBufferMemory, 0));

    return true;
}

void VulkanBuffer::transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size)
{
    VkCommandBuffer cmdBuf = render->beginIsolateCommands();
    transfer(cmdBuf, srcBuffer, dstBuffer, size);
    render->endIsolateCommands(cmdBuf);
}

void VulkanBuffer::transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size)
{

    // VulkanUtils::transitionImageLayout(
    //     render->getDevice(),
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
}
