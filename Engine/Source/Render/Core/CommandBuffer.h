#pragma once

#include "Render/RenderDefines.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include <cstdint>
#include <cstring>
#include <utility>
#include <variant>
#include <vector>

// Command buffer execution mode:
// 0 = Virtual function mode (direct vkCmd* calls through virtual functions)
// 1 = Recording mode (record commands to vector, batch execute later)
// Use this to profile and compare performance
#ifndef YA_CMDBUF_RECORD_MODE
    #define YA_CMDBUF_RECORD_MODE 0
#endif



namespace ya
{


// Forward declarations
struct IGraphicsPipeline;
struct IRenderPass;
struct IBuffer;
struct IImage;
struct IRenderTarget;


#if YA_CMDBUF_RECORD_MODE
struct RenderCommand
{
    struct BindPipeline
    {
        IGraphicsPipeline* pipeline = nullptr;
    };
    struct BindVertexBuffer
    {
        uint32_t       binding = 0;
        const IBuffer* buffer  = nullptr;
        uint64_t       offset  = 0;
    };
    struct BindIndexBuffer
    {
        IBuffer* buffer          = nullptr;
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
        IPipelineLayout*                 pipelineLayout = nullptr;
        uint32_t                         firstSet       = 0;
        std::vector<DescriptorSetHandle> descriptorSets;
        std::vector<uint32_t>            dynamicOffsets;
    };
    struct PushConstants
    {
        IPipelineLayout*     pipelineLayout = nullptr;
        EShaderStage::T      stages         = {};
        uint32_t             offset         = 0;
        std::vector<uint8_t> data;
    };
    struct CopyBuffer
    {
        IBuffer* src       = nullptr;
        IBuffer* dst       = nullptr;
        uint64_t size      = 0;
        uint64_t srcOffset = 0;
        uint64_t dstOffset = 0;
    };
    struct BeginRendering
    {
        RenderingInfo info;
    };
    struct EndRendering
    {
    };
    struct TransitionImageLayout
    {
        void*                 image     = nullptr;
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
#endif


/**
 * @brief Generic command buffer interface for recording GPU commands
 *
 * Two execution modes controlled by YA_CMDBUF_RECORD_MODE:
 * - Mode 0 (Virtual): Direct virtual function calls to backend (vkCmd*)
 * - Mode 1 (Record): Record commands to vector, batch execute via executeAll()
 */
struct ICommandBuffer
{
#if YA_CMDBUF_RECORD_MODE
    std::vector<RenderCommand> recordedCommands;
#endif

  public:
    virtual ~ICommandBuffer() = default;

    ICommandBuffer()                                 = default;
    ICommandBuffer(const ICommandBuffer&)            = delete;
    ICommandBuffer& operator=(const ICommandBuffer&) = delete;
    ICommandBuffer(ICommandBuffer&&)                 = default;
    ICommandBuffer& operator=(ICommandBuffer&&)      = default;

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

#if YA_CMDBUF_RECORD_MODE
    // ========== Recording Mode: Push commands to vector ==========
    void bindPipeline(IGraphicsPipeline* pipeline)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindPipeline{pipeline}});
    }
#else
    // ========== Virtual Mode: Direct virtual function calls ==========
    virtual void bindPipeline(IGraphicsPipeline* pipeline) = 0;
#endif

#if YA_CMDBUF_RECORD_MODE
    // ========== Recording Mode Implementations ==========
    void bindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindVertexBuffer{binding, buffer, offset}});
    }

    void bindIndexBuffer(IBuffer* buffer, uint64_t offset = 0, bool use16BitIndices = false)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BindIndexBuffer{buffer, offset, use16BitIndices}});
    }

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::Draw{vertexCount, instanceCount, firstVertex, firstInstance}});
    }

    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::DrawIndexed{indexCount, instanceCount, firstIndex, vertexOffset, firstInstance}});
    }

    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetViewPort{x, y, width, height, minDepth, maxDepth}});
    }

    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::SetScissor{x, y, width, height}});
    }

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

    void bindDescriptorSets(
        IPipelineLayout*                        pipelineLayout,
        uint32_t                                firstSet,
        const std::vector<DescriptorSetHandle>& descriptorSets,
        const std::vector<uint32_t>&            dynamicOffsets = {})
    {
        RenderCommand::BindDescriptorSets cmd{
            .pipelineLayout = pipelineLayout,
            .firstSet       = firstSet,
            .descriptorSets = descriptorSets,
            .dynamicOffsets = dynamicOffsets,
        };
        recordedCommands.push_back(RenderCommand{std::move(cmd)});
    }

    void pushConstants(
        IPipelineLayout* pipelineLayout,
        EShaderStage::T  stages,
        uint32_t         offset,
        uint32_t         size,
        const void*      data)
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

    void copyBuffer(IBuffer* src, IBuffer* dst, uint64_t size,
                    uint64_t srcOffset = 0, uint64_t dstOffset = 0)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::CopyBuffer{src, dst, size, srcOffset, dstOffset}});
    }

    void beginRendering(const RenderingInfo& info)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::BeginRendering{info}});
    }

    void endRendering()
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::EndRendering{}});
    }

    void transitionImageLayout(
        void*                        image,
        EImageLayout::T              oldLayout,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange& subresourceRange)
    {
        recordedCommands.push_back(RenderCommand{RenderCommand::TransitionImageLayout{image, oldLayout, newLayout, subresourceRange}});
    }

    virtual void executeAll()
    {
        recordedCommands.clear();
    }

#else
    // ========== Virtual Mode Declarations ==========
    virtual void bindVertexBuffer(uint32_t binding, const IBuffer* buffer, uint64_t offset = 0)      = 0;
    virtual void bindIndexBuffer(IBuffer* buffer, uint64_t offset = 0, bool use16BitIndices = false) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0)                          = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0)                                             = 0;
    virtual void setViewport(float x, float y, float width, float height,
                             float minDepth = 0.0f, float maxDepth = 1.0f)                           = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)                   = 0;
    virtual void setCullMode(ECullMode::T cullMode)                                                  = 0;
    virtual void setPolygonMode(EPolygonMode::T polygonMode)                                         = 0;
    virtual void bindDescriptorSets(IPipelineLayout*                        pipelineLayout,
                                    uint32_t                                firstSet,
                                    const std::vector<DescriptorSetHandle>& descriptorSets,
                                    const std::vector<uint32_t>&            dynamicOffsets = {})                = 0;
    virtual void pushConstants(IPipelineLayout* pipelineLayout,
                               EShaderStage::T  stages,
                               uint32_t         offset,
                               uint32_t         size,
                               const void*      data)                                                     = 0;
    virtual void copyBuffer(IBuffer* src, IBuffer* dst, uint64_t size,
                            uint64_t srcOffset = 0, uint64_t dstOffset = 0)                          = 0;

    /**
     * @brief Copy data from buffer to image
     * @param srcBuffer Source buffer
     * @param dstImage Destination image
     * @param dstImageLayout Current layout of destination image (must be TransferDst)
     * @param regions Copy regions
     */
    virtual void copyBufferToImage(
        IBuffer*                            srcBuffer,
        IImage*                             dstImage,
        EImageLayout::T                     dstImageLayout,
        const std::vector<BufferImageCopy>& regions) = 0;

    virtual void beginRendering(const RenderingInfo& info) = 0;

    /**
     * @brief End rendering (works for both RenderPass and Dynamic Rendering)
     */
    virtual void endRendering(const EndRenderingInfo& info = {}) = 0;

    virtual void transitionImageLayout(
        IImage*                      image,
        EImageLayout::T              oldLayout,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange* subresourceRange = nullptr) = 0;

    virtual void transitionImageLayoutAuto(
        IImage*                      image,
        EImageLayout::T              newLayout,
        const ImageSubresourceRange* subresourceRange = nullptr) = 0;

    // Helper: Transition all attachments of a RenderTarget to specified layouts
    virtual void transitionRenderTargetLayout(
        IRenderTarget*  renderTarget,
        EImageLayout::T colorLayout,
        EImageLayout::T depthLayout   = EImageLayout::Undefined,
        EImageLayout::T stencilLayout = EImageLayout::Undefined) = 0;

    virtual void debugBeginLabel(const char* labelName, const float* colorRGBA = nullptr) = 0;
    virtual void debugEndLabel()                                                          = 0;

    #if YA_CMDBUF_RECORD_MODE
    // No-op in virtual mode - commands are executed immediately
    virtual void executeAll() { recordedCommands.clear(); }
    #endif
#endif
};

} // namespace ya