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
    VkBuffer           _handle      = VK_NULL_HANDLE;
    VkDeviceMemory     _memory      = VK_NULL_HANDLE;
    VkBufferUsageFlags _usageFlags  = 0;
    VkDeviceSize       _size        = 0;
    bool               bHostVisible = false; // CPU can access the memory directly



    virtual ~VulkanBuffer();



    static auto create(VulkanRender *render, const BufferCreateInfo &ci)
    {
        auto ret          = std::make_shared<VulkanBuffer>();
        ret->_render      = render;
        ret->_usageFlags  = ci.usage;
        ret->name         = ci.debugName;
        ret->bHostVisible = ci.memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        ret->_size        = ci.size;

        if (ci.data.has_value()) {
            ret->createWithDataInternal(ci.data.value(), static_cast<uint32_t>(ci.size), ci.memProperties);
        }
        else {
            ret->createDefaultInternal(static_cast<uint32_t>(ci.size), ci.memProperties);
        }
        ret->setupDebugName(ret->name);

        return ret;
    }


    bool writeData(const void *data, uint32_t size, uint32_t offset = 0);
    bool flush(uint32_t size = 0, uint32_t offset = 0);

    template <typename T>
    T *map()
    {
        void *data = nullptr;
        mapInternal(&data);
        return static_cast<T *>(data);
    }
    void unmap();

    static bool allocate(VulkanRender *render, uint32_t size,
                         VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage,
                         VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    // do a copy pass
    static void transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);
    static void transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);

    [[nodiscard]] VkBuffer       getHandle() const { return _handle; }
    [[nodiscard]] VkDeviceMemory getMemory() const { return _memory; }

  private:
    void createWithDataInternal(const void *data, uint32_t size, VkMemoryPropertyFlags memProperties);
    void createDefaultInternal(uint32_t size, VkMemoryPropertyFlags memProperties);

    void mapInternal(void **ptr);
    void setupDebugName(const std::string &name);
};