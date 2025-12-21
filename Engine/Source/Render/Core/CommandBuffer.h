#pragma once

#include "Render/RenderDefines.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include <variant>
#include <vector>



namespace ya
{


// Forward declarations
struct IGraphicsPipeline;
struct IRenderPass;
struct IBuffer;


struct RenderCommand
{
    struct BindPipeline
    {
        IGraphicsPipeline *pipeline = nullptr;
    };
    struct SetViewPort
    {
        float x        = 0.0f;
        float y        = 0.0f;
        float width    = 0.0f;
        float height   = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    using type = std::variant<
        BindPipeline,
        SetViewPort>;
    type data;
};


/**
 * @brief Generic command buffer interface for recording GPU commands
 */
struct ICommandBuffer
{

    std::vector<RenderCommand> recordedCommands;

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

    virtual void bindPipeline(IGraphicsPipeline *pipeline) = 0;
    /**
     * @brief Bind a vertex buffer
     */
    virtual void bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset = 0) = 0;

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
        IPipelineLayout                        *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets = {}) = 0;

    /**
     * @brief Push constants
     */
    virtual void pushConstants(
        IPipelineLayout *pipelineLayout,
        EShaderStage::T  stages,
        uint32_t         offset,
        uint32_t         size,
        const void      *data) = 0;

    /**
     * @brief Copy buffer to buffer
     */
    virtual void copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                            uint64_t srcOffset = 0, uint64_t dstOffset = 0) = 0;

    // ========================================================================
    // Dynamic Rendering Commands (Vulkan 1.3+ / VK_KHR_dynamic_rendering)
    // ========================================================================

    /**
     * @brief Begin dynamic rendering
     * @param info Dynamic rendering configuration
     * @note Replaces render pass begin in dynamic rendering mode
     */
    virtual void beginRendering(const DynamicRenderingInfo &info) = 0;

    /**
     * @brief End dynamic rendering
     * @note Replaces render pass end in dynamic rendering mode
     */
    virtual void endRendering() = 0;

    /**
     * @brief Transition image layout
     * @param image Backend-specific image handle
     * @param oldLayout Old image layout
     * @param newLayout New image layout
     * @param subresourceRange Subresource range to transition
     * @note Required for manual layout transitions in dynamic rendering
     */
    virtual void transitionImageLayout(
        void                        *image,
        EImageLayout::T              oldLayout,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange &subresourceRange) = 0;


    // TODO: backend override this method to execute recorded commands, batch call, but not virtual call each cmd
    //      然后做个性能对比，暂时不做提前优化
    virtual void executeRenderCommands()
    {
        TODO("Implement command execution in derived classes, not use this example");
        for (const auto &cmd : recordedCommands) {
            std::visit(
                [&](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, RenderCommand::BindPipeline>) {
                    }
                    else if constexpr (std::is_same_v<T, RenderCommand::SetViewPort>) {
                    }
                },
                cmd.data);
        }
    }
};

} // namespace ya