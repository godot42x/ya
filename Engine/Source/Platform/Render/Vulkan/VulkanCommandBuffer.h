
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
    VulkanRender*   _render        = nullptr;
    VkCommandBuffer _commandBuffer = VK_NULL_HANDLE;
    bool            _isRecording   = false;

    // Track current rendering mode for proper endRendering() call
    ERenderingMode::T _currentRenderingMode = ERenderingMode::None;

    // Static function pointers for VK_KHR_dynamic_rendering
    static PFN_vkCmdBeginRenderingKHR s_vkCmdBeginRenderingKHR;
    static PFN_vkCmdEndRenderingKHR   s_vkCmdEndRenderingKHR;

    // Static function pointer for VK_EXT_extended_dynamic_state3
    static PFN_vkCmdSetPolygonModeEXT s_vkCmdSetPolygonModeEXT;

    friend struct VulkanRender; // Allow VulkanRender to initialize the function pointers


    void executeBindPipeline(IGraphicsPipeline* pipeline);
    void executeBindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset);
    void executeBindIndexBuffer(IBuffer* buffer, uint64_t offset, bool use16BitIndices);
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

    // === Rendering helpers ===
    void beginRenderingWithRenderPass(IRenderTarget* renderTarget, const RenderingInfo& info);
    void beginDynamicRenderingFromRenderTarget(IRenderTarget* renderTarget, const RenderingInfo& info);
    void beginDynamicRenderingFromManualImages(const RenderingInfo& info);

    // Helper: Build depth attachment info for dynamic rendering
    VkRenderingAttachmentInfo* buildDepthAttachmentInfo(const RenderingInfo&       info,
                                                        VkRenderingAttachmentInfo& outDepthAttach);

    // Helper: Execute dynamic rendering with prepared attachments
    void executeDynamicRendering(std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                                 VkRenderingAttachmentInfo*              pDepthAttach,
                                 const VkRect2D&                         renderArea,
                                 uint32_t                                layerCount = 1);

    void executeEndRendering(const EndRenderingInfo& info);

    void executeBindDescriptorSets(IPipelineLayout*                        pipelineLayout,
                                   uint32_t                                firstSet,
                                   const std::vector<DescriptorSetHandle>& descriptorSets,
                                   const std::vector<uint32_t>&            dynamicOffsets);
    void executePushConstants(IPipelineLayout* pipelineLayout,
                              EShaderStage::T  stages,
                              uint32_t         offset,
                              uint32_t         size,
                              const void*      data);
    void executeCopyBuffer(IBuffer* src, IBuffer* dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset);
    void executeTransitionImageLayout(IImage* image, EImageLayout::T oldLayout, EImageLayout::T newLayout,
                                      const ImageSubresourceRange* subresourceRange);

  public:
    VulkanCommandBuffer(VulkanRender* render, VkCommandBuffer commandBuffer)
        : _render(render), _commandBuffer(commandBuffer) {}

    ~VulkanCommandBuffer() override = default;

    VulkanCommandBuffer(const VulkanCommandBuffer&)            = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&)                 = default;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&)      = default;

    // ICommandBuffer interface
    CommandBufferHandle getHandle() const override { return CommandBufferHandle(_commandBuffer); }

    CommandBufferHandle getTypedHandle() const override
    {
        return CommandBufferHandle((void*)(uintptr_t)_commandBuffer);
    }

    VkCommandBuffer getVkHandle() const { return _commandBuffer; }

    bool begin(bool oneTimeSubmit = false) override;
    bool end() override;
    void reset() override;

#if YA_CMDBUF_RECORD_MODE
    void executeAll() override;
#endif

    // Virtual mode: direct implementations
    void bindPipeline(IGraphicsPipeline* pipeline) override;
    void bindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset = 0) override;
    void bindIndexBuffer(IBuffer* buffer, uint64_t offset = 0, bool use16BitIndices = false) override;
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
    void bindDescriptorSets(IPipelineLayout*                        pipelineLayout,
                            uint32_t                                firstSet,
                            const std::vector<DescriptorSetHandle>& descriptorSets,
                            const std::vector<uint32_t>&            dynamicOffsets = {}) override;
    void pushConstants(IPipelineLayout* pipelineLayout,
                       EShaderStage::T  stages,
                       uint32_t         offset,
                       uint32_t         size,
                       const void*      data) override;
    void copyBuffer(IBuffer* src, IBuffer* dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0) override;
    void copyBufferToImage(IBuffer* srcBuffer, IImage* dstImage, EImageLayout::T dstImageLayout,
                           const std::vector<BufferImageCopy>& regions) override;
    void beginRendering(const RenderingInfo& info) override;
    void endRendering(const EndRenderingInfo& info) override;
    void transitionImageLayout(IImage* image, EImageLayout::T oldLayout, EImageLayout::T newLayout,
                               const ImageSubresourceRange* subresourceRange) override;
    void transitionImageLayoutAuto(IImage* image, EImageLayout::T newLayout, const ImageSubresourceRange* subresourceRange = nullptr) override;


    // Helper: Transition all attachments of a RenderTarget to specified layouts
    void transitionRenderTargetLayout(
        IRenderTarget*  renderTarget,
        EImageLayout::T colorLayout,
        EImageLayout::T depthLayout   = EImageLayout::Undefined,
        EImageLayout::T stencilLayout = EImageLayout::Undefined) override;
};



struct VulkanCommandPool : disable_copy
{
    VkCommandPool _handle = VK_NULL_HANDLE;

    VulkanRender* _render = nullptr;
    VulkanQueue*  _queue  = nullptr;

    VulkanCommandPool(VulkanRender* render, VulkanQueue* queue, VkCommandPoolCreateFlags flags = 0);

    bool allocateCommandBuffer(VkCommandBufferLevel level, VkCommandBuffer& outCommandBuffer);

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