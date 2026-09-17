#include "Render3D/Common/PipelineCommon.h"

#include "RHI/Core/Image.h"
#include "RHI/Core/ImageResource.h"

namespace ya
{

std::shared_ptr<ImageResource> makeShadowDebugResource(const std::shared_ptr<IImage>& image,
                                                       const std::shared_ptr<IImageView>& view,
                                                       std::string_view label)
{
    if (!image || !view) {
        return nullptr;
    }

    auto resource         = std::make_shared<ImageResource>();
    resource->label       = std::string(label);
    resource->image       = image;
    resource->defaultView = view;
    resource->retainedResources = {RetainedResource{image}, RetainedResource{view}};
    return resource;
}

} // namespace ya