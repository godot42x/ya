// OneLineFormatOffRegex: ^(// NOLINT)

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
    std::string           label;
};

struct VulkanBuffer
{
    VulkanRender *_render = nullptr;

    std::string        name         = "None";
    VkBuffer           _handle      = VK_NULL_HANDLE;
    VkDeviceMemory     _memory      = VK_NULL_HANDLE;
    VkBufferUsageFlags _usageFlags  = 0;
    VkDeviceSize       _size        = 0;
    bool               bHostVisible = false; // CPU can access the memory directly



    // clang-format off
  private: inline static constexpr struct { } _dummy; public: // make it can only be created on heap // NOLINT
    // clang-format on

    VulkanBuffer(decltype(_dummy), VulkanRender *render, const BufferCreateInfo &ci)
    {
        _render      = render;
        _usageFlags  = ci.usage;
        name         = ci.label;
        bHostVisible = ci.memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        _size        = ci.size;

        if (ci.data.has_value()) {
            createWithDataInternal(ci.data.value(), static_cast<uint32_t>(ci.size), ci.memProperties);
        }
        else {
            createDefaultInternal(static_cast<uint32_t>(ci.size), ci.memProperties);
        }

        YA_CORE_TRACE("Created VulkanBuffer [{}]: {} of size: {} with usage: {}", name, (uintptr_t)_handle, _size, std::to_string(_usageFlags));
        setupDebugName(name);
    }
    virtual ~VulkanBuffer();

    static auto create(VulkanRender *render, const BufferCreateInfo &ci)
    {
        return makeShared<VulkanBuffer>(_dummy, render, ci);
    }


    bool writeData(const void *data, uint32_t size = 0, uint32_t offset = 0);
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

  protected:
  private:
    void createWithDataInternal(const void *data, uint32_t size, VkMemoryPropertyFlags memProperties);
    void createDefaultInternal(uint32_t size, VkMemoryPropertyFlags memProperties);

    void mapInternal(void **ptr);
    void setupDebugName(const std::string &name);
};
