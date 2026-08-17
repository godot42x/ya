#include "ImageResource.h"

namespace ya
{

std::shared_ptr<ImageResource> createImageResource(
    IRenderResourceFactory& factory,
    const ImageResourceDesc& desc)
{
    auto image = factory.createImage(desc.image);
    if (!image) {
        YA_CORE_ERROR("Failed to create image resource '{}'", desc.image.label);
        return nullptr;
    }

    auto view = factory.createImageView(image, desc.defaultView);
    if (!view) {
        YA_CORE_ERROR("Failed to create default view for image resource '{}'", desc.image.label);
        return nullptr;
    }

    auto resource         = std::make_shared<ImageResource>();
    resource->label       = desc.image.label;
    resource->desc        = desc;
    resource->image       = std::move(image);
    resource->defaultView = std::move(view);
    return resource;
}

} // namespace ya
