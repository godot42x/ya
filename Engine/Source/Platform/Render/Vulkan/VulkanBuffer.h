// OneLineFormatOffRegex: ^(// NOLINT)

#pragma once
#include "Core/Base.h"
#include "Platform/Render/Vulkan/VulkanUtils.h"
#include "Render/Core/Buffer.h"


#include <vulkan/vulkan.h>


namespace ya
{

struct VulkanRender;

struct VulkanBuffer : public ya::IBuffer
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
        _render         = render;
        _usageFlags     = toVk(ci.usage);
        name            = ci.label;
        auto vkMemProps = toVk(ci.memProperties);
        bHostVisible    = vkMemProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        _size           = ci.size;

        if (ci.data.has_value()) {
            createWithDataInternal(ci.data.value(), static_cast<uint32_t>(ci.size), vkMemProps);
        }
        else {
            createDefaultInternal(static_cast<uint32_t>(ci.size), vkMemProps);
        }

        // YA_CORE_TRACE("Created VulkanBuffer [{}]: {} of size: {} with usage: {}", name, (uintptr_t)_handle, _size, std::to_string(_usageFlags));
        setupDebugName(name);
    }
    virtual ~VulkanBuffer();

    static auto create(VulkanRender *render, const BufferCreateInfo &ci)
    {
        return std::shared_ptr<VulkanBuffer>(new VulkanBuffer(_dummy, render, ci));
    }


    // IBuffer interface implementation
    bool               writeData(const void *data, uint32_t size = 0, uint32_t offset = 0) override;
    bool               flush(uint32_t size = 0, uint32_t offset = 0) override;
    void               unmap() override;
    BufferHandle       getHandle() const override { return BufferHandle(_handle); }
    uint32_t           getSize() const override { return static_cast<uint32_t>(_size); }
    bool               isHostVisible() const override { return bHostVisible; }
    const std::string &getName() const override { return name; }

    // Vulkan-specific methods
    [[nodiscard]] VkBuffer       getVkBuffer() const { return _handle; }
    [[nodiscard]] VkDeviceMemory getVkMemory() const { return _memory; }

    static bool allocate(VulkanRender *render, uint32_t size,
                         VkMemoryPropertyFlags memProperties, VkBufferUsageFlags usage,
                         VkBuffer &outBuffer, VkDeviceMemory &outBufferMemory);

    // do a copy pass
    static void transfer(VulkanRender *render, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);
    static void transfer(VkCommandBuffer cmdBuf, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t size);

  protected:
    void mapInternal(void **ptr) override;

  private:
    void createWithDataInternal(const void *data, uint32_t size, VkMemoryPropertyFlags memProperties);
    void createDefaultInternal(uint32_t size, VkMemoryPropertyFlags memProperties);
    void setupDebugName(const std::string &name);
};

} // namespace ya