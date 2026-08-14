#include "GUI/Compose/GUIRenderSurface.h"

#include "RHI/Core/RenderResourceFactory.h"

#include <format>

namespace ya
{

GUIRenderSurface::GUIRenderSurface(std::shared_ptr<RenderImage> image, EImageLayout::T finalLayout)
    : _image(std::move(image))
    , _finalLayout(finalLayout)
{
}

std::shared_ptr<GUIRenderSurface> GUIRenderSurface::createOffscreen(
    IRenderResourceFactory&      factory,
    const FGUIRenderSurfaceDesc& desc)
{
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.colorFormat == EFormat::Undefined) {
        return nullptr;
    }

    const std::string label = desc.label.empty() ? "GUIRenderSurface" : desc.label;
    auto image = createRenderImage(
        factory,
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = label,
                .format        = desc.colorFormat,
                .extent        = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = desc.usage,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = std::format("{}_DefaultView", label),
                .viewType    = EImageViewType::View2D,
                .aspectFlags = EImageAspect::Color,
            },
        });
    return image ? std::shared_ptr<GUIRenderSurface>(new GUIRenderSurface(std::move(image), desc.finalLayout))
                 : nullptr;
}

std::shared_ptr<GUIRenderSurface> GUIRenderSurface::wrapExternal(
    std::shared_ptr<RenderImage> image,
    EImageLayout::T              finalLayout)
{
    if (!image || !image->isValid()) {
        return nullptr;
    }
    return std::shared_ptr<GUIRenderSurface>(new GUIRenderSurface(std::move(image), finalLayout));
}

bool GUIRenderSurface::isValid() const
{
    return _image && _image->isValid();
}

void GUIRenderSurface::prepare(const FRender2DComposePassDesc& passDesc, EFormat::T depthFormat) const
{
    if (isValid()) {
        prepareRender2DComposePassPipeline(passDesc, _image->getFormat(), depthFormat);
    }
}

void GUIRenderSurface::record(ICommandBuffer*              cmdBuf,
                              RenderImage*                 depthTarget,
                              const UIFrameSnapshot*       uiFrameSnapshot,
                              FRender2DComposePassDesc     passDesc,
                              const std::function<void()>& extraContent) const
{
    if (!isValid()) {
        return;
    }
    passDesc.finalLayout = _finalLayout;
    recordRender2DComposePass(cmdBuf, *_image, depthTarget, uiFrameSnapshot, passDesc, extraContent);
}

} // namespace ya
