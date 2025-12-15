#pragma once

#include "Core/Base.h"
#include "Render/Core/CommandBuffer.h"
#include "glad/glad.h"

#include <memory>
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

    // Command recording (for deferred execution if needed)
    struct Command
    {
        enum Type
        {
            BindPipeline,
            BindVertexBuffer,
            BindIndexBuffer,
            Draw,
            DrawIndexed,
            SetViewport,
            SetScissor,
            SetCullMode,
            BindDescriptorSets,
            PushConstants,
            CopyBuffer,
        };

        Type     type;
        uint32_t data[16]; // Generic data storage
        void    *ptr;      // Pointer to additional data if needed
    };

    std::vector<Command> _commands;

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

    // OpenGL-specific: execute all recorded commands
    void execute();
};

} // namespace ya
