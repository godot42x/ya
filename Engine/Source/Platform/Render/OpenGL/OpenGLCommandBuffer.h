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

    void executeAll() override;
};

} // namespace ya
