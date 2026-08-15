
#pragma once

#include "CommandBuffer.h"
#include "Core/Base.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

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
    stdptr<IImage>              resolveImage;

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

    // FrameBuffer directly owns attachment images/views.
    std::vector<std::shared_ptr<RenderImage>> _colorAttachments;
    std::shared_ptr<RenderImage>              _depthAttachment;
    std::shared_ptr<RenderImage>              _resolveAttachment;

    // Legacy compatibility views for callers that still consume Texture.
    mutable std::vector<std::shared_ptr<Texture>> _colorTextures;
    mutable std::shared_ptr<Texture>              _depthTexture;
    mutable std::shared_ptr<Texture>              _resolveTexture;

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

    const std::vector<std::shared_ptr<RenderImage>>& getColorAttachments() const { return _colorAttachments; }
    RenderImage*                                     getColorAttachment(uint32_t attachmentIdx) const;
    std::shared_ptr<RenderImage>                     getColorAttachmentShared(uint32_t attachmentIdx) const;
    RenderImage*                                     getDepthAttachment() const { return _depthAttachment.get(); }
    RenderImage*                                     getResolveAttachment() const { return _resolveAttachment.get(); }
    const std::shared_ptr<RenderImage>&              getDepthAttachmentShared() const { return _depthAttachment; }
    const std::shared_ptr<RenderImage>&              getResolveAttachmentShared() const { return _resolveAttachment; }

    const std::vector<std::shared_ptr<Texture>>& getColorTextures() const
    {
        if (_colorTextures.size() != _colorAttachments.size()) {
            rebuildTextureCompatCache();
        }
        return _colorTextures;
    }
    Texture*                                     getColorTexture(uint32_t attachmentIdx) const;
    Texture*                                     getDepthTexture() const;
    Texture*                                     getResolveTexture() const;


  protected:
    void clearAttachments();
    void rebuildTextureCompatCache() const;
};

} // namespace ya
