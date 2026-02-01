
#pragma once
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

    // Static function pointer for VK_EXT_extended_dynamic_state3
    static PFN_vkCmdSetPolygonModeEXT s_vkCmdSetPolygonModeEXT;

    friend struct VulkanRender; // Allow VulkanRender to initialize the function pointer

#if YA_CMDBUF_RECORD_MODE
    // Internal execute methods for recording mode
    void executeBindPipeline(IGraphicsPipeline *pipeline);
    void executeBindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset);
    void executeBindIndexBuffer(IBuffer *buffer, uint64_t offset, bool use16BitIndices);
    void executeDraw(uint32_t vertexCount, uint32_t instanceCount,
                     uint32_t firstVertex, uint32_t firstInstance);
    void executeDrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                            uint32_t firstIndex, int32_t vertexOffset,
                            uint32_t firstInstance);
    void executeSetViewport(float x, float y, float width, float height,
                            float minDepth, float maxDepth);
    void executeSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void executeSetCullMode(ECullMode::T cullMode);
    void executeSetPolygonMode(EPolygonMode::T polygonMode);
    void executeBindDescriptorSets(
        IPipelineLayout                        *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets);
    void executePushConstants(
        IPipelineLayout *pipelineLayout,
        EShaderStage::T  stages,
        uint32_t         offset,
        uint32_t         size,
        const void      *data);
    void executeCopyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                           uint64_t srcOffset, uint64_t dstOffset);
#endif

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

#if YA_CMDBUF_RECORD_MODE
    void executeAll() override;
#else
    // Virtual mode: direct implementations
    void bindPipeline(IGraphicsPipeline *pipeline) override;
    void bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset = 0) override;
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
    void setPolygonMode(EPolygonMode::T polygonMode) override;
    void bindDescriptorSets(
        IPipelineLayout                        *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets = {}) override;
    void pushConstants(
        IPipelineLayout *pipelineLayout,
        EShaderStage::T  stages,
        uint32_t         offset,
        uint32_t         size,
        const void      *data) override;
    void copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0) override;
    void beginRendering(const DynamicRenderingInfo &info) override;
    void endRendering() override;
    void transitionImageLayout(
        void                        *image,
        EImageLayout::T              oldLayout,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange &subresourceRange) override;
    void executeAll() override {}
#endif
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