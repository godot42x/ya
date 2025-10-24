#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/RenderDefines.h"
#include <vector>

namespace ya
{


// Forward declarations
class IGraphicsPipeline;
class IRenderPass;
class IBuffer;

/**
 * @brief Type-safe command buffer handle
 */
struct CommandBufferHandleTag
{};
using CommandBufferHandle = Handle<CommandBufferHandleTag>;

/**
 * @brief Generic command buffer interface for recording GPU commands
 */
class ICommandBuffer
{
  public:
    virtual ~ICommandBuffer() = default;

    ICommandBuffer()                                  = default;
    ICommandBuffer(const ICommandBuffer &)            = delete;
    ICommandBuffer &operator=(const ICommandBuffer &) = delete;
    ICommandBuffer(ICommandBuffer &&)                 = default;
    ICommandBuffer &operator=(ICommandBuffer &&)      = default;

    /**
     * @brief Get the native handle (backend-specific)
     */
    virtual CommandBufferHandle getHandle() const = 0;

    /**
     * @brief Get typed native handle
     */
    template <typename T>
    T getHandleAs() const { return getHandle().as<T>(); }

    /**
     * @brief Get type-safe handle
     */
    virtual CommandBufferHandle getTypedHandle() const = 0;

    /**
     * @brief Begin recording commands
     */
    virtual bool begin(bool oneTimeSubmit = false) = 0;

    /**
     * @brief End recording commands
     */
    virtual bool end() = 0;

    /**
     * @brief Reset the command buffer
     */
    virtual void reset() = 0;

    /**
     * @brief Bind a vertex buffer
     */
    virtual void bindVertexBuffer(uint32_t binding, IBuffer *buffer, uint64_t offset = 0) = 0;

    /**
     * @brief Bind an index buffer
     */
    virtual void bindIndexBuffer(IBuffer *buffer, uint64_t offset = 0, bool use16BitIndices = false) = 0;

    /**
     * @brief Draw command
     */
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

    /**
     * @brief Draw indexed command
     */
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    /**
     * @brief Set viewport
     */
    virtual void setViewport(float x, float y, float width, float height,
                             float minDepth = 0.0f, float maxDepth = 1.0f) = 0;

    /**
     * @brief Set scissor rectangle
     */
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    /**
     * @brief Set cull mode (for dynamic pipeline state)
     */
    virtual void setCullMode(ECullMode::T cullMode) = 0;

    /**
     * @brief Bind descriptor sets
     */
    virtual void bindDescriptorSets(
        void                                   *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets = {}) = 0;

    /**
     * @brief Push constants
     */
    virtual void pushConstants(
        void           *pipelineLayout,
        EShaderStage::T stages,
        uint32_t        offset,
        uint32_t        size,
        const void     *data) = 0;

    /**
     * @brief Copy buffer to buffer
     */
    virtual void copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                            uint64_t srcOffset = 0, uint64_t dstOffset = 0) = 0;
};

} // namespace ya
