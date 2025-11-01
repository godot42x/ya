
#pragma once
#include "Core/Base.h"
#include "Core/Log.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "VulkanUtils.h"


#include <vulkan/vulkan.h>


namespace ya
{

struct VulkanRender;
struct VulkanQueue;


/**
 * @brief Vulkan implementation of ICommandBuffer
 */
class VulkanCommandBuffer : public ICommandBuffer
{
  private:
    VulkanRender   *_render        = nullptr;
    VkCommandBuffer _commandBuffer = VK_NULL_HANDLE;
    bool            _isRecording   = false;

  public:
    VulkanCommandBuffer(VulkanRender *render, VkCommandBuffer commandBuffer)
        : _render(render), _commandBuffer(commandBuffer) {}

    ~VulkanCommandBuffer() override = default;

    VulkanCommandBuffer(const VulkanCommandBuffer &)            = delete;
    VulkanCommandBuffer &operator=(const VulkanCommandBuffer &) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer &&)                 = default;
    VulkanCommandBuffer &operator=(VulkanCommandBuffer &&)      = default;

    // ICommandBuffer interface
    CommandBufferHandle getHandle() const override { return CommandBufferHandle(_commandBuffer); }

    CommandBufferHandle getTypedHandle() const override
    {
        return CommandBufferHandle((void *)(uintptr_t)_commandBuffer);
    }

    VkCommandBuffer getVkHandle() const { return _commandBuffer; }

    bool begin(bool oneTimeSubmit = false) override;
    bool end() override;
    void reset() override;

    void bindPipeline(IGraphicsPipeline *pipeline) override;
    void bindVertexBuffer(uint32_t binding, IBuffer *buffer, uint64_t offset = 0) override;
    void bindIndexBuffer(IBuffer *buffer, uint64_t offset = 0, bool use16BitIndices = false) override;

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;

    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;

    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f) override;

    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void setCullMode(ECullMode::T cullMode) override;

    void bindDescriptorSets(
        void                                   *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets = {}) override;

    void pushConstants(
        void           *pipelineLayout,
        EShaderStage::T stages,
        uint32_t        offset,
        uint32_t        size,
        const void     *data) override;

    void copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0) override;
};



struct VulkanCommandPool : disable_copy
{
    VkCommandPool _handle = VK_NULL_HANDLE;

    VulkanRender *_render = nullptr;
    VulkanQueue  *_queue  = nullptr;

    VulkanCommandPool(VulkanRender *render, VulkanQueue *queue, VkCommandPoolCreateFlags flags = 0);

    bool allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer &outCommandBuffer);

    void cleanup();

    static void begin(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags flags = 0)
    {
        VkCommandBufferBeginInfo beginInfo{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = flags,
            .pInheritanceInfo = nullptr,
        };
        VK_CALL(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    }

    static void end(VkCommandBuffer commandBuffer)
    {
        VK_CALL(vkEndCommandBuffer(commandBuffer));
    }
};

} // namespace ya