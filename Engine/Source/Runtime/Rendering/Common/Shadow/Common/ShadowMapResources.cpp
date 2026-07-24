#include "ShadowMapResources.h"

#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowViewBuilder.h"

namespace ya
{

void ShadowMapResources::init(IRender* render, const ShadowMapResourceDesc& desc)
{
    YA_CORE_ASSERT(render, "ShadowMapResources requires render device");

    extent = desc.extent;
    depthFormat = desc.depthFormat;
    layerCount = getShadowTotalLayerCount();

    auto* resourceFactory = render->getResourceFactory();
    depthImage = resourceFactory->createImage(ImageCreateInfo{
        .label       = std::string(desc.imageLabel),
        .format      = desc.depthFormat,
        .extent      = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
        .mipLevels   = 1,
        .arrayLayers = layerCount,
        .samples     = ESampleCount::Sample_1,
        .usage       = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
        .initialLayout = EImageLayout::Undefined,
        .flags         = EImageCreateFlag::CubeCompatible,
    });
    YA_CORE_ASSERT(depthImage, "Failed to create shadow depth image");

    sampler = resourceFactory->createSampler(SamplerDesc{
        .label        = std::string(desc.samplerLabel),
        .minFilter    = EFilter::Linear,
        .magFilter    = EFilter::Linear,
        .mipmapMode   = ESamplerMipmapMode::Linear,
        .addressModeU = ESamplerAddressMode::ClampToBorder,
        .addressModeV = ESamplerAddressMode::ClampToBorder,
        .addressModeW = ESamplerAddressMode::ClampToBorder,
        .borderColor  = SamplerDesc::BorderColor{.type = SamplerDesc::EBorderColor::FloatOpaqueWhite, .color = {1, 1, 1, 1}},
    });

    rebuildViews(render, desc.viewLabelPrefix);
}

void ShadowMapResources::destroy()
{
    directionalDepthIV.reset();
    for (auto& imageView : pointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : pointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }
    depthImage.reset();
    sampler.reset();
    extent = {};
    depthFormat = EFormat::Undefined;
    layerCount = 0;
}

void ShadowMapResources::rebuildViews(IRender* render, std::string_view viewLabelPrefix)
{
    YA_CORE_ASSERT(render, "ShadowMapResources requires render device");
    YA_CORE_ASSERT(depthImage, "ShadowMapResources requires shadow depth image");

    directionalDepthIV.reset();
    for (auto& imageView : pointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : pointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }

    auto* resourceFactory = render->getResourceFactory();
    auto views         = ShadowViewBuilder::buildLayerViews(resourceFactory, depthImage, viewLabelPrefix);
    directionalDepthIV = std::move(views.directionalDepthIV);
    pointCubeIVs       = std::move(views.pointCubeIVs);
    pointFaceIVs       = std::move(views.pointFaceIVs);
}

} // namespace ya
