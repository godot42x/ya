#pragma once
#include "Core/Base.h"

#include <vulkan/vulkan.h>



struct VulkanRender;

struct VulkanBuffer
{
    VulkanRender *_render = nullptr;

    VkBuffer           _handle     = VK_NULL_HANDLE;
    VkDeviceMemory     _memory     = VK_NULL_HANDLE;
    VkBufferUsageFlags _usageFlags = 0;
    VkDeviceSize       _size       = 0;



    virtual ~VulkanBuffer();

    static auto create(VulkanRender *render, VkBufferUsageFlags usage, std::size_t size, const void *data)
    {
        auto ret         = std::make_shared<VulkanBuffer>();
        ret->_render     = render;
        ret->_usageFlags = usage;


        ret->createWithDataInternal(data, static_cast<uint32_t>(size));


        return ret;
    }
    static auto create(VulkanRender *render, VkBufferUsageFlags usage, std::size_t size)
    {
        auto ret         = std::make_shared<VulkanBuffer>();
        ret->_render     = render;
        ret->_usageFlags = usage;

        ret->createDefaultInternal(static_cast<uint32_t>(size));

        return ret;
    }



    void createWithDataInternal(const void *data, uint32_t size);
    void createDefaultInternal(uint32_t size);



    static bool allocate(VulkanRender *render, uint32_t size,
                         VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage,
                         VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    // do a copy pass
    static void transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);
    static void transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);

    [[nodiscard]] VkBuffer       getHandle() const { return _handle; }
    [[nodiscard]] VkDeviceMemory getMemory() const { return _memory; }
};