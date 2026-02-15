
#pragma once

#include "CommandBuffer.h"
#include "Core/Base.h"
#include "Render/Core/Texture.h"
#include "Render/RenderDefines.h"

#include <memory>

namespace ya
{

// Forward declarations
struct IRender;
struct IRenderPass;

// /**
//  * @brief FrameBuffer attachment specification
//  */
// struct FrameBufferAttachmentInfo
// {
//     EFormat::T                     format  = EFormat::Undefined;
//     EImageUsage::T                 usage   = EImageUsage::None;
//     bool                           isDepth = false; // true for depth attachment, false for color
//     std::optional<ESampleCount::T> msaaSamples;     // Optional: MSAA sample count
// };

struct FrameBufferCreateInfo
{
    std::string label  = "None";
    uint32_t    width  = 0;
    uint32_t    height = 0;

    // Mode 1: Attachment specifications for FrameBuffer to create textures
    // std::vector<FrameBufferAttachmentInfo>   colorAttachments;
    // std::optional<FrameBufferAttachmentInfo> depthAttachment;

    // Mode 2: External images for swapchain or externally-managed attachments
    // If provided, these take precedence and will be wrapped into Textures
    std::vector<stdptr<IImage>> colorImages;
    stdptr<IImage>              depthImages;
    stdptr<IImage>              resolveImages;

    // render pass api spec
    IRenderPass* renderPass = nullptr;
};

/**
 * @brief Abstract interface for frame buffers
 * Frame buffers represent render targets with attachments
 */
struct IFrameBuffer
{
    std::string _label = "None";

    // FrameBuffer directly owns Textures for all attachments
    std::vector<std::shared_ptr<Texture>> _colorTextures;
    std::shared_ptr<Texture>              _depthTexture;
    std::shared_ptr<Texture>              _resolveTexture;

    IRenderPass* _renderPass = nullptr;

    virtual ~IFrameBuffer() = default;

    virtual Extent2D getExtent() const = 0;
    uint32_t         getWidth() const { return getExtent().width; }
    uint32_t         getHeight() const { return getExtent().height; }

    static stdptr<IFrameBuffer> create(IRender* render, const FrameBufferCreateInfo& createInfo);
    bool                        recreate(const FrameBufferCreateInfo& ci)
    {
        _label      = ci.label;
        _renderPass = ci.renderPass;
        return onRecreate(ci);
    }

    virtual bool begin(ICommandBuffer* commandBuffer) = 0;
    virtual bool end(ICommandBuffer* commandBuffer)   = 0;

    virtual bool onRecreate(const FrameBufferCreateInfo& ci) = 0;

    // renderpass api spec
    virtual void* getHandle() const = 0;
    template <typename T>
    T getHandleAs() const { return static_cast<T>(getHandle()); }

    // ===== Direct Texture Access =====

    const std::vector<std::shared_ptr<Texture>>& getColorTextures() const { return _colorTextures; }
    Texture*                                     getColorTexture(uint32_t attachmentIdx) const;
    Texture*                                     getDepthTexture() const { return _depthTexture.get(); }
    Texture*                                     getResolveTexture() const { return _resolveTexture.get(); }


  protected:
    void clearAttachments();
};

} // namespace ya
