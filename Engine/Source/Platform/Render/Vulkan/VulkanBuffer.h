#pragma once
#include "Core/Base.h"

#include <vulkan/vulkan.h>



struct VulkanRender;

struct BufferCreateInfo
{
    VkBufferUsageFlags usage;
    // do not set nullptr, if you want empty buffer, set data to std::nullopt
    std::optional<void *> data = std::nullopt;
    uint32_t              size;
    VkMemoryPropertyFlags memProperties;
    std::string           debugName;
};

struct VulkanBuffer
{
    VulkanRender *_render = nullptr;

    std::string        name;
    VkBuffer           _handle     = VK_NULL_HANDLE;
    VkDeviceMemory     _memory     = VK_NULL_HANDLE;
    VkBufferUsageFlags _usageFlags = 0;
    VkDeviceSize       _size       = 0;



    virtual ~VulkanBuffer();



    static auto create(VulkanRender *render, const BufferCreateInfo &ci)
    {
        auto ret         = std::make_shared<VulkanBuffer>();
        ret->_render     = render;
        ret->_usageFlags = ci.usage;
        ret->name        = ci.debugName;

        if (ci.data.has_value()) {
            ret->createWithDataInternal(ci.data.value(), static_cast<uint32_t>(ci.size), ci.memProperties);
        }
        else {
            ret->createDefaultInternal(static_cast<uint32_t>(ci.size), ci.memProperties);
        }

        return ret;
    }

    void createWithDataInternal(const void *data, uint32_t size, VkMemoryPropertyFlags memProperties);
    void createDefaultInternal(uint32_t size, VkMemoryPropertyFlags memProperties);



    static bool allocate(VulkanRender *render, uint32_t size,
                         VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage,
                         VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    // do a copy pass
    static void transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);
    static void transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);

    [[nodiscard]] VkBuffer       getHandle() const { return _handle; }
    [[nodiscard]] VkDeviceMemory getMemory() const { return _memory; }
};