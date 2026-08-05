#include "VulkanBuffer.h"
#include "VulkanUtils.h"

#include "VulkanMemoryAllocator.h"

#include "VulkanRender.h"

namespace ya
{


VulkanBuffer::~VulkanBuffer()
{
    if (_handle != VK_NULL_HANDLE && _allocation != VK_NULL_HANDLE) {
        if (bMemoryMapped) {
            vmaUnmapMemory(_render->getVmaAllocator(), _allocation);
        }
        vmaDestroyBuffer(_render->getVmaAllocator(), _handle, _allocation);
        _handle     = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::createWithDataInternal(const void* data, uint32_t size, EMemoryUsage memUsage)
{

    VkBuffer      stageBuffer     = nullptr;
    VmaAllocation stageAllocation = nullptr;

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           EMemoryUsage::CpuToGpu,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           stageBuffer,
                           stageAllocation);

    void* mappedData = nullptr;
    VK_CALL(vmaMapMemory(_render->getVmaAllocator(), stageAllocation, &mappedData));
    std::memcpy(mappedData, data, size);
    vmaUnmapMemory(_render->getVmaAllocator(), stageAllocation);

    VulkanBuffer::allocate(_render,
                           static_cast<uint32_t>(size),
                           EMemoryUsage::GpuOnly,
                           _usageFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           _handle,
                           _allocation);

    VulkanBuffer::transfer(_render, stageBuffer, _handle, size, "fromRowData");

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

bool VulkanBuffer::writeData(const void* data, uint32_t size, uint32_t offset)
{
    if (!data) {
        YA_CORE_ERROR("Write data to buffer {} failed: data is nullptr", name);
        return false;
    }
    if (!bHostVisible) {
        YA_CORE_ERROR("Write data to buffer {} failed: buffer is not host visible", name);
        return false;
    }

    const VkDeviceSize writeSize   = size == 0 ? _size : size;
    const VkDeviceSize writeOffset = offset;
    if (size == 0 && offset != 0) {
        YA_CORE_ERROR("Write data to buffer {} failed: size==0 requires offset==0", name);
        return false;
    }
    if (writeOffset + writeSize > _size) {
        YA_CORE_ERROR("Write data to buffer {} failed: range [{}, {}) exceeds size {}",
                      name,
                      static_cast<uint64_t>(writeOffset),
                      static_cast<uint64_t>(writeOffset + writeSize),
                      static_cast<uint64_t>(_size));
        return false;
    }

    void* mappedData = nullptr;
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
    if (!bHostVisible) {
        YA_CORE_ERROR("Flush buffer {} failed: buffer is not host visible", name);
        return false;
    }

    if (bHostCoherent) {
        return true;
    }

    if (!bMemoryMapped) {
        YA_CORE_ERROR("Flush buffer {} failed: buffer memory must be mapped before flush", name);
        return false;
    }

    const VkDeviceSize flushSize = size == 0 ? _size : size;
    if (size == 0 && offset != 0) {
        YA_CORE_ERROR("Flush buffer {} failed: size==0 requires offset==0", name);
        return false;
    }
    if (static_cast<VkDeviceSize>(offset) + flushSize > _size) {
        YA_CORE_ERROR("Flush buffer {} failed: range [{}, {}) exceeds size {}",
                      name,
                      static_cast<uint64_t>(offset),
                      static_cast<uint64_t>(static_cast<VkDeviceSize>(offset) + flushSize),
                      static_cast<uint64_t>(_size));
        return false;
    }

    vmaFlushAllocation(_render->getVmaAllocator(), _allocation, offset, flushSize);
    return true;
}


void VulkanBuffer::mapInternal(void** ptr)
{
    YA_CORE_ASSERT(bHostVisible, "Buffer is not host visible, cannot map!");
    YA_CORE_ASSERT(!bMemoryMapped, "Buffer memory is already mapped!");
    VK_CALL(vmaMapMemory(_render->getVmaAllocator(), _allocation, ptr));
    bMemoryMapped = true;
    if (!bHostCoherent) {
        // Readback contract (FG-802): make prior GPU writes visible to the CPU
        // before the caller reads mapped memory. Harmless for write-only maps.
        vmaInvalidateAllocation(_render->getVmaAllocator(), _allocation, 0, VK_WHOLE_SIZE);
    }
}

void VulkanBuffer::setupDebugName(const std::string& inName)
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

bool VulkanBuffer::allocate(VulkanRender* render, uint32_t size, EMemoryUsage memUsage, VkBufferUsageFlags usage, VkBuffer& outBuffer, VmaAllocation& outAllocation)
{
    VkBufferCreateInfo vkBufferCI{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = size,
        .usage                 = usage,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
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

void VulkanBuffer::transfer(VulkanRender* render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size, const std::string& ctx)
{
    auto*           cmdBuf   = render->beginIsolateCommands(std::format(
        "BufferTransfer:{}:src=0x{:x}:dst=0x{:x}:size={}",
        ctx,
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
