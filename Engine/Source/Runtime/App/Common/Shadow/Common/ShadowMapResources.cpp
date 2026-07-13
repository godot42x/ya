#include "ShadowMapResources.h"

#include "Render/Core/Texture.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Runtime/App/Common/Shadow/Common/ShadowViewBuilder.h"

namespace ya
{

void ShadowMapResources::init(IRender* render, const ShadowMapResourceDesc& desc)
{
    YA_CORE_ASSERT(render, "ShadowMapResources requires render device");

    extent = desc.extent;

    renderTarget = createRenderTarget(RenderTargetCreateInfo{
        .label            = std::string(desc.renderTargetLabel),
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = desc.extent,
        .frameBufferCount = 1,
        .layerCount       = 1 + MAX_POINT_LIGHTS * 6,
        .attachments      = {
            .depthAttach = AttachmentDescription{
                .index            = 0,
                .format           = desc.depthFormat,
                .samples          = ESampleCount::Sample_1,
                .loadOp           = EAttachmentLoadOp::Clear,
                .storeOp          = EAttachmentStoreOp::Store,
                .initialLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
                .usage            = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
                .imageCreateFlags = EImageCreateFlag::CubeCompatible,
            },
        },
    });
    YA_CORE_ASSERT(renderTarget, "Failed to create shadow render target");

    sampler = render->getResourceFactory()->createSampler(SamplerDesc{
        .label        = std::string(desc.samplerLabel),
        .minFilter    = EFilter::Linear,
        .magFilter    = EFilter::Linear,
        .mipmapMode   = ESamplerMipmapMode::Linear,
        .addressModeU = ESamplerAddressMode::ClampToBorder,
        .addressModeV = ESamplerAddressMode::ClampToBorder,
        .addressModeW = ESamplerAddressMode::ClampToBorder,
        .borderColor  = SamplerDesc::BorderColor{.type = SamplerDesc::EBorderColor::FloatOpaqueWhite, .color = {1, 1, 1, 1}},
    });

    layerCount = 1 + MAX_POINT_LIGHTS * 6;
    rebuildViews(render, desc.viewLabelPrefix);
}

void ShadowMapResources::destroy()
{
    depthImage.reset();
    extent = {};
    layerCount = 0;
    directionalDepthIV.reset();
    for (auto& imageView : pointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : pointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }
    sampler.reset();
    renderTarget.reset();
}

void ShadowMapResources::rebuildViews(IRender* render, std::string_view viewLabelPrefix)
{
    YA_CORE_ASSERT(render, "ShadowMapResources requires render device");
    YA_CORE_ASSERT(renderTarget, "ShadowMapResources requires shadow render target");

    directionalDepthIV.reset();
    for (auto& imageView : pointCubeIVs) {
        imageView.reset();
    }
    for (auto& faceViews : pointFaceIVs) {
        for (auto& imageView : faceViews) {
            imageView.reset();
        }
    }

    auto* frameBuffer  = renderTarget->getCurFrameBuffer();
    auto* depthTexture = frameBuffer ? frameBuffer->getDepthTexture() : nullptr;
    YA_CORE_ASSERT(depthTexture, "Shadow render target depth texture is null");

    auto* resourceFactory = render->getResourceFactory();
    depthImage            = depthTexture->getImageShared();
    YA_CORE_ASSERT(depthImage, "Shadow render target image is null");

    auto views         = ShadowViewBuilder::buildLayerViews(resourceFactory, depthImage, viewLabelPrefix);
    directionalDepthIV = std::move(views.directionalDepthIV);
    pointCubeIVs       = std::move(views.pointCubeIVs);
    pointFaceIVs       = std::move(views.pointFaceIVs);
}

} // namespace ya
