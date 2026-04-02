#include "VulkanBuffer.h"
#include "VulkanUtils.h"

#include <vk_mem_alloc.h>

#include "VulkanRender.h"

namespace ya
{


VulkanBuffer::~VulkanBuffer()
{
    if (_handle != VK_NULL_HANDLE && _allocation != VK_NULL_HANDLE) {
        if(bMemoryMapped){
            vmaUnmapMemory(_render->getVmaAllocator(), _allocation);
        }
        vmaDestroyBuffer(_render->getVmaAllocator(), _handle, _allocation);
        _handle     = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::createWithDataInternal(const void *data, uint32_t size, EMemoryUsage memUsage)
{

    VkBuffer       stageBuffer     = nullptr;
    VmaAllocation  stageAllocation = nullptr;

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           EMemoryUsage::CpuToGpu,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           stageBuffer,
                           stageAllocation);

    void *mappedData = nullptr;
    VK_CALL(vmaMapMemory(_render->getVmaAllocator(), stageAllocation, &mappedData));
    std::memcpy(mappedData, data, size);
    vmaUnmapMemory(_render->getVmaAllocator(), stageAllocation);

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           EMemoryUsage::GpuOnly,
                           _usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           _handle,
                           _allocation);

    VulkanBuffer::transfer(_render, stageBuffer, _handle, size);

    vmaDestroyBuffer(_render->getVmaAllocator(), stageBuffer, stageAllocation);
}

void VulkanBuffer::createDefaultInternal(uint32_t size, EMemoryUsage memUsage)
{
    VulkanBuffer::allocate(_render,
                           size,
                           memUsage,
                           _usageFlags,
                           _handle,
                           _allocation);
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

    VkDeviceSize writeSize = size == 0 ? _size : size;
    VkDeviceSize writeOffset = offset;
    YA_CORE_ASSERT(writeOffset + writeSize <= _size, "Write data out of range!");
    if (size == 0) {
        YA_CORE_ASSERT(offset == 0, "If size is 0, offset must be 0");
    }

    void *mappedData = nullptr;
    VK_CALL(vmaMapMemory(_render->getVmaAllocator(), _allocation, &mappedData));
    bMemoryMapped = true;

    std::memcpy(static_cast<char*>(mappedData) + writeOffset, data, static_cast<size_t>(writeSize));

    if (!bHostCoherent) {
        vmaFlushAllocation(_render->getVmaAllocator(), _allocation, writeOffset, size == 0 ? VK_WHOLE_SIZE : writeSize);
    }

    vmaUnmapMemory(_render->getVmaAllocator(), _allocation);
    bMemoryMapped = false;
    return true;
}

bool VulkanBuffer::flush(uint32_t size, uint32_t offset)
{
    YA_CORE_ASSERT(bHostVisible, "Buffer is not host visible, cannot flush!");

    if (bHostCoherent) {
        return true;
    }

    YA_CORE_ASSERT(bMemoryMapped, "Buffer memory must be mapped before flush!");

    vmaFlushAllocation(_render->getVmaAllocator(), _allocation, offset, size == 0 ? VK_WHOLE_SIZE : size);
    return true;
}


void VulkanBuffer::mapInternal(void **ptr)
{
    YA_CORE_ASSERT(bHostVisible, "Buffer is not host visible, cannot map!");
    YA_CORE_ASSERT(!bMemoryMapped, "Buffer memory is already mapped!");
    VK_CALL(vmaMapMemory(_render->getVmaAllocator(), _allocation, ptr));
    bMemoryMapped = true;
}

void VulkanBuffer::setupDebugName(const std::string &inName)
{
    if (!inName.empty()) {
        _render->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, _handle, inName.c_str());
        vmaSetAllocationName(_render->getVmaAllocator(), _allocation, inName.c_str());
    }
}

void VulkanBuffer::unmap()
{
    if (!bMemoryMapped) {
        return;
    }

    vmaUnmapMemory(_render->getVmaAllocator(), _allocation);
    bMemoryMapped = false;
}

bool VulkanBuffer::allocate(VulkanRender *render, uint32_t size, EMemoryUsage memUsage, VkBufferUsageFlags usage, VkBuffer &outBuffer, VmaAllocation &outAllocation)
{
    VkBufferCreateInfo vkBufferCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size  = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
    };

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_AUTO;
    switch (memUsage) {
    case EMemoryUsage::GpuOnly:
        break;
    case EMemoryUsage::CpuToGpu:
        allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case EMemoryUsage::GpuToCpu:
        allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        break;
    default:
        break;
    }

    VK_CALL(vmaCreateBuffer(render->getVmaAllocator(), &vkBufferCI, &allocCI, &outBuffer, &outAllocation, nullptr));

    return true;
}

void VulkanBuffer::transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size)
{
    auto *cmdBuf = render->beginIsolateCommands(std::format(
        "BufferTransfer:src=0x{:x}:dst=0x{:x}:size={}",
        reinterpret_cast<uintptr_t>(srcBuffer),
        reinterpret_cast<uintptr_t>(dstBuffer),
        size));
    VkCommandBuffer vkCmdBuf = cmdBuf->getHandleAs<VkCommandBuffer>();
    transfer(vkCmdBuf, srcBuffer, dstBuffer, size);
    render->endIsolateCommands(cmdBuf);
}

void VulkanBuffer::transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size)
{

    VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = size,
    };
    vkCmdCopyBuffer(cmdBuf, srcBuffer, dstBuffer, 1, &copyRegion);
}

} // namespace ya
