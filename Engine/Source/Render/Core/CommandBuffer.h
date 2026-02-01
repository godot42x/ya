#pragma once

#include "Render/RenderDefines.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include <cstdint>
#include <cstring>
#include <utility>
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
    struct BindVertexBuffer
    {
        uint32_t       binding = 0;
        const IBuffer *buffer  = nullptr;
        uint64_t       offset  = 0;
    };
    struct BindIndexBuffer
    {
        IBuffer *buffer          = nullptr;
        uint64_t offset          = 0;
        bool     use16BitIndices = false;
    };
    struct Draw
    {
        uint32_t vertexCount   = 0;
        uint32_t instanceCount = 1;
        uint32_t firstVertex   = 0;
        uint32_t firstInstance = 0;
    };
    struct DrawIndexed
    {
        uint32_t indexCount    = 0;
        uint32_t instanceCount = 1;
        uint32_t firstIndex    = 0;
        int32_t  vertexOffset  = 0;
        uint32_t firstInstance = 0;
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
    struct SetScissor
    {
        int32_t  x      = 0;
        int32_t  y      = 0;
        uint32_t width  = 0;
        uint32_t height = 0;
    };
    struct SetCullMode
    {
        ECullMode::T cullMode = ECullMode::Back;
    };
    struct SetPolygonMode
    {
        EPolygonMode::T polygonMode = EPolygonMode::Fill;
    };
    struct BindDescriptorSets
    {
        IPipelineLayout                 *pipelineLayout = nullptr;
        uint32_t                         firstSet       = 0;
        std::vector<DescriptorSetHandle> descriptorSets;
        std::vector<uint32_t>            dynamicOffsets;
    };
    struct PushConstants
    {
        IPipelineLayout     *pipelineLayout = nullptr;
        EShaderStage::T      stages         = {};
        uint32_t             offset         = 0;
        std::vector<uint8_t> data;
    };
    struct CopyBuffer
    {
        IBuffer *src       = nullptr;
        IBuffer *dst       = nullptr;
        uint64_t size      = 0;
        uint64_t srcOffset = 0;
        uint64_t dstOffset = 0;
    };
    struct BeginRendering
    {
        DynamicRenderingInfo info;
    };
    struct EndRendering
    {
    };
    struct TransitionImageLayout
    {
        void                 *image     = nullptr;
        EImageLayout::T       oldLayout = EImageLayout::Undefined;
        EImageLayout::T       newLayout = EImageLayout::Undefined;
        ImageSubresourceRange subresourceRange;
    };

    using type = std::variant<
        BindPipeline,
        BindVertexBuffer,
        BindIndexBuffer,
        Draw,
        DrawIndexed,
        SetViewPort,
        SetScissor,
        SetCullMode,
        SetPolygonMode,
        BindDescriptorSets,
        PushConstants,
        CopyBuffer,
        BeginRendering,
        EndRendering,
        TransitionImageLayout>;
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

    void bindPipeline(IGraphicsPipeline *pipeline)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindPipeline{pipeline}});
    }
    /**
     * @brief Bind a vertex buffer
     */
    void bindVertexBuffer(uint32_t binding, const IBuffer *buffer, uint64_t offset = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindVertexBuffer{binding, buffer, offset}});
    }

    /**
     * @brief Bind an index buffer
     */
    void bindIndexBuffer(IBuffer *buffer, uint64_t offset = 0, bool use16BitIndices = false)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindIndexBuffer{buffer, offset, use16BitIndices}});
    }

    /**
     * @brief Draw command
     */
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::Draw{vertexCount, instanceCount, firstVertex, firstInstance}});
    }

    /**
     * @brief Draw indexed command
     */
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::DrawIndexed{indexCount, instanceCount, firstIndex, vertexOffset, firstInstance}});
    }

    /**
     * @brief Set viewport
     */
    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetViewPort{x, y, width, height, minDepth, maxDepth}});
    }

    /**
     * @brief Set scissor rectangle
     */
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetScissor{x, y, width, height}});
    }

    /**
     * @brief Set cull mode (for dynamic pipeline state)
     */
    void setCullMode(ECullMode::T cullMode)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetCullMode{cullMode}});
    }

    /**
     * @brief Set polygon mode (Fill, Line, Point)
     */
    void setPolygonMode(EPolygonMode::T polygonMode)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetPolygonMode{polygonMode}});
    }

    /**
     * @brief Bind descriptor sets
     */
    void bindDescriptorSets(
        IPipelineLayout                        *pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle> &descriptorSets,
        const std::vector<uint32_t>            &dynamicOffsets = {})
    {
        RenderCommand::BindDescriptorSets cmd{
            .pipelineLayout = pipelineLayout,
            .firstSet       = firstSet,
            .descriptorSets = descriptorSets,
            .dynamicOffsets = dynamicOffsets,
        };
        recordedCommands.push_back(RenderCommand{std::move(cmd)});
    }

    /**
     * @brief Push constants
     */
    void pushConstants(
        IPipelineLayout *pipelineLayout,
        EShaderStage::T  stages,
        uint32_t         offset,
        uint32_t         size,
        const void      *data)
    {
        RenderCommand::PushConstants cmd;
        cmd.pipelineLayout = pipelineLayout;
        cmd.stages         = stages;
        cmd.offset         = offset;
        cmd.data.resize(size);
        if (size > 0 && data) {
            std::memcpy(cmd.data.data(), data, size);
        }
        recordedCommands.push_back(RenderCommand{std::move(cmd)});
    }

    /**
     * @brief Copy buffer to buffer
     */
    void copyBuffer(IBuffer *src, IBuffer *dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::CopyBuffer{src, dst, size, srcOffset, dstOffset}});
    }

    // ========================================================================
    // Dynamic Rendering Commands (Vulkan 1.3+ / VK_KHR_dynamic_rendering)
    // ========================================================================

    /**
     * @brief Begin dynamic rendering
     * @param info Dynamic rendering configuration
     * @note Replaces render pass begin in dynamic rendering mode
     */
    void beginRendering(const DynamicRenderingInfo &info)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BeginRendering{info}});
    }

    /**
     * @brief End dynamic rendering
     * @note Replaces render pass end in dynamic rendering mode
     */
    void endRendering()
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::EndRendering{}});
    }

    /**
     * @brief Transition image layout
     * @param image Backend-specific image handle
     * @param oldLayout Old image layout
     * @param newLayout New image layout
     * @param subresourceRange Subresource range to transition
     * @note Required for manual layout transitions in dynamic rendering
     */
    void transitionImageLayout(
        void                        *image,
        EImageLayout::T              oldLayout,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange &subresourceRange)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::TransitionImageLayout{image, oldLayout, newLayout, subresourceRange}});
    }


    // TODO: backend override this method to execute recorded commands, batch call, but not virtual call each cmd
    //      然后做个性能对比，暂时不做提前优化
    virtual void executeAll()
    {
        recordedCommands.clear();
    }
};

} // namespace ya