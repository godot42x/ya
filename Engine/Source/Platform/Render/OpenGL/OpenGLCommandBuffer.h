#pragma once

#include "Render/Core/CommandBuffer.h"
#include "glad/glad.h"

#include <vector>

namespace ya
{

struct OpenGLRender;
struct IGraphicsPipeline;
struct OpenGLPipeline;

/**
 * @brief OpenGL implementation of ICommandBuffer
 *
 * In OpenGL, commands are typically executed immediately.
 * This class provides a deferred command recording system
 * to match the Vulkan-like interface.
 */
class OpenGLCommandBuffer : public ICommandBuffer
{
  private:
    OpenGLRender *_render      = nullptr;
    bool          _isRecording = false;

    // Current state
    OpenGLPipeline *_currentPipeline = nullptr;

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
    OpenGLCommandBuffer(OpenGLRender *render)
        : _render(render) {}

    ~OpenGLCommandBuffer() override = default;

    OpenGLCommandBuffer(const OpenGLCommandBuffer &)            = delete;
    OpenGLCommandBuffer &operator=(const OpenGLCommandBuffer &) = delete;
    OpenGLCommandBuffer(OpenGLCommandBuffer &&)                 = default;
    OpenGLCommandBuffer &operator=(OpenGLCommandBuffer &&)      = default;

    // ICommandBuffer interface
    CommandBufferHandle getHandle() const override { return CommandBufferHandle((void *)this); }

    CommandBufferHandle getTypedHandle() const override
    {
        return CommandBufferHandle((void *)this);
    }

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
#endif
};

} // namespace ya
