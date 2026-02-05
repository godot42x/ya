
#pragma once

#include "CommandBuffer.h"
#include "Core/Base.h"
#include "Render/Core/Image.h"
#include "Render/RenderDefines.h"


namespace ya
{

// Forward declarations
struct IRender;
struct IRenderPass;

struct FrameBufferCreateInfo
{
    std::string                 label  = "None";
    uint32_t                    width  = 0;
    uint32_t                    height = 0;
    std::vector<stdptr<IImage>> colorImages;
    stdptr<IImage>              depthImage;
    // stdptr<IImage>              stencilImage;

    // render pass api spec
    IRenderPass *renderPass;
};

/**
 * @brief Abstract interface for frame buffers
 * Frame buffers represent render targets with attachments
 */
struct IFrameBuffer
{
    std::string                 _label = "None";
    std::vector<stdptr<IImage>> _colorImages;
    stdptr<IImage>              _depthImage;
    // stdptr<IImage>              _stencilImage;


    std::vector<stdptr<IImageView>> _colorImageViews;
    stdptr<IImageView>              _depthImageView;
    // stdptr<IImageView>              _stencilImageView;

    IRenderPass *_renderPass = nullptr;

    virtual ~IFrameBuffer() = default;



    virtual Extent2D getExtent() const = 0;
    uint32_t         getWidth() const { return getExtent().width; }
    uint32_t         getHeight() const { return getExtent().height; }

    static stdptr<IFrameBuffer> create(IRender *render, const FrameBufferCreateInfo &createInfo);
    bool                        recreate(const FrameBufferCreateInfo &ci)
    {
        _label       = ci.label;
        _colorImages = ci.colorImages;
        _depthImage  = ci.depthImage;
        // _stencilImage = ci.stencilImage;
        _renderPass = ci.renderPass;
        return onRecreate(ci);
    }

    virtual bool begin(ICommandBuffer *commandBuffer) = 0;
    virtual bool end(ICommandBuffer *commandBuffer)   = 0;

    virtual bool onRecreate(const FrameBufferCreateInfo &ci) = 0;
    // renderpass api spec
    virtual void *getHandle() const = 0;
    template <typename T>
    T getHandleAs() const
    {
        return static_cast<T>(getHandle());
    }
    const std::vector<stdptr<IImage>>     &getColorImages() const { return _colorImages; }
    const std::vector<stdptr<IImageView>> &getColorImageViews() const { return _colorImageViews; }


    IImageView *getColorImageView(uint32_t attachmentIdx) { return _colorImageViews[attachmentIdx].get(); }
    IImage     *getColorImage(uint32_t attachmentIdx) { return _colorImages[attachmentIdx].get(); }
    IImageView *getDepthImageView() { return _depthImageView.get(); }
    IImage     *getDepthImage() { return _depthImage.get(); }
    // IImageView *getStencilImageView() { return _stencilImageView.get(); }
    // IImage     *getStencilImage() { return _stencilImage.get(); }
};

} // namespace ya
