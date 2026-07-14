#include "RenderImage.h"

namespace ya
{

std::shared_ptr<RenderImage> createRenderImage(
    IRenderResourceFactory& factory,
    const RenderImageDesc& desc)
{
    auto image = factory.createImage(desc.image);
    if (!image) {
        YA_CORE_ERROR("Failed to create render image '{}'", desc.image.label);
        return nullptr;
    }

    auto view = factory.createImageView(image, desc.defaultView);
    if (!view) {
        YA_CORE_ERROR("Failed to create default view for render image '{}'", desc.image.label);
        return nullptr;
    }

    auto resource         = std::make_shared<RenderImage>();
    resource->label       = desc.image.label;
    resource->image       = std::move(image);
    resource->defaultView = std::move(view);
    return resource;
}

} // namespace ya
