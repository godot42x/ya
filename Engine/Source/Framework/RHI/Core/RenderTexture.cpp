#include "RenderTexture.h"

namespace ya
{

namespace
{

EImageAspect::T resolveAspectFlags(const RenderTextureCreateInfo& ci)
{
    return ci.isDepth ? EImageAspect::Depth : EImageAspect::Color;
}

EImageViewType::T resolveViewType(const RenderTextureCreateInfo& ci)
{
    if (ci.layerCount == 6 && !ci.isDepth) {
        return EImageViewType::ViewCube;
    }

    if (ci.layerCount > 1) {
        return EImageViewType::View2DArray;
    }

    return EImageViewType::View2D;
}

ImageResourceDesc makeImageResourceDesc(const RenderTextureCreateInfo& ci)
{
    return ImageResourceDesc{
        .image = ImageCreateInfo{
            .label       = ci.label,
            .format      = ci.format,
            .extent      = {
                .width  = ci.width,
                .height = ci.height,
                .depth  = 1,
            },
            .mipLevels   = ci.mipLevels,
            .arrayLayers = ci.layerCount,
            .samples     = ci.samples,
            .usage       = ci.usage,
            .initialLayout = EImageLayout::Undefined,
            .flags       = (ci.layerCount == 6 && !ci.isDepth)
                               ? EImageCreateFlag::CubeCompatible
                               : EImageCreateFlag::None,
        },
        .defaultView = ImageViewCreateInfo{
            .label          = ci.label.empty() ? std::string{} : ci.label + ".defaultView",
            .viewType       = resolveViewType(ci),
            .aspectFlags    = resolveAspectFlags(ci),
            .baseMipLevel   = 0,
            .levelCount     = ci.mipLevels,
            .baseArrayLayer = 0,
            .layerCount     = ci.layerCount,
        },
    };
}

std::shared_ptr<ImageResource> makeWrappedImageResource(
    std::shared_ptr<IImage> image,
    std::shared_ptr<IImageView> view,
    const std::string& label)
{
    if (!image || !view) {
        YA_CORE_ERROR("Failed to wrap render texture '{}': missing image or image view", label);
        return nullptr;
    }

    auto resource         = std::make_shared<ImageResource>();
    resource->label       = label;
    resource->image       = std::move(image);
    resource->defaultView = std::move(view);
    resource->retainedResources.push_back(resource->image);
    resource->retainedResources.push_back(resource->defaultView);
    return resource;
}

} // namespace

std::shared_ptr<RenderTexture> RenderTexture::create(
    IRenderResourceFactory& factory,
    const RenderTextureCreateInfo& ci)
{
    YA_CORE_ASSERT(ci.width > 0, "RenderTexture::create requires width > 0");
    YA_CORE_ASSERT(ci.height > 0, "RenderTexture::create requires height > 0");
    YA_CORE_ASSERT(ci.layerCount > 0, "RenderTexture::create requires layerCount > 0");
    YA_CORE_ASSERT(ci.mipLevels > 0, "RenderTexture::create requires mipLevels > 0");

    auto resource = createImageResource(factory, makeImageResourceDesc(ci));
    if (!resource) {
        return nullptr;
    }

    auto texture     = std::make_shared<RenderTexture>();
    texture->resource = std::move(resource);
    return texture;
}

std::shared_ptr<RenderTexture> RenderTexture::wrap(
    std::shared_ptr<IImage> image,
    std::shared_ptr<IImageView> view,
    const std::string& label)
{
    auto resource = makeWrappedImageResource(std::move(image), std::move(view), label);
    if (!resource) {
        return nullptr;
    }

    return adopt(std::move(resource));
}

std::shared_ptr<RenderTexture> RenderTexture::adopt(
    std::shared_ptr<ImageResource> imageResource,
    const std::string& label)
{
    if (!imageResource) {
        YA_CORE_ERROR("Failed to adopt render texture: image resource is null");
        return nullptr;
    }

    if (!label.empty() && imageResource->label.empty()) {
        imageResource->label = label;
    }

    auto texture     = std::make_shared<RenderTexture>();
    texture->resource = std::move(imageResource);
    return texture;
}

} // namespace ya
