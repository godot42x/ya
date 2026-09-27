#include "GUI/Compose/GUIRenderSurface.h"

#include "RHI/Core/RenderResourceFactory.h"

#include <format>

namespace ya
{

GUIRenderSurface::GUIRenderSurface(std::shared_ptr<RenderTexture> image, EImageLayout::T finalLayout)
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
    auto image = RenderTexture::create(
        factory,
        RenderTextureCreateInfo{
            .label   = label,
            .width   = desc.extent.width,
            .height  = desc.extent.height,
            .format  = desc.colorFormat,
            .usage   = desc.usage,
            .samples = ESampleCount::Sample_1,
        });
    return image ? std::shared_ptr<GUIRenderSurface>(new GUIRenderSurface(std::move(image), desc.finalLayout))
                 : nullptr;
}

std::shared_ptr<GUIRenderSurface> GUIRenderSurface::wrapExternal(
    std::shared_ptr<RenderTexture> image,
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

void GUIRenderSurface::prepare(ScreenDrawRecorder& recorder, EFormat::T depthFormat) const
{
    if (isValid()) {
        prepareRender2DComposePassPipeline(recorder, _image->getFormat(), depthFormat);
    }
}

void GUIRenderSurface::record(ICommandBuffer*              cmdBuf,
                              const UIFrameSnapshot*       uiFrameSnapshot,
                              FRender2DComposePassDesc     passDesc,
                              ScreenDrawRecorder&          recorder,
                              const std::function<void(ScreenDrawList&)>& extraContent) const
{
    if (!isValid()) {
        return;
    }
    passDesc.finalLayout = _finalLayout;
    recordRender2DComposePass(cmdBuf, *_image, uiFrameSnapshot, passDesc, recorder, extraContent);
}

} // namespace ya
