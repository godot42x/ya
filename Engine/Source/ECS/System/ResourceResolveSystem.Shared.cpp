#include "ResourceResolveSystem.Detail.h"

#include "Render/Core/RenderResourceFactory.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Runtime/App/App.h"
#include "Runtime/App/Utility/OffscreenJobRunner.h"


#include <algorithm>
#include <format>

namespace ya::detail
{

void retireTexture(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), std::move(texture));
    texture = nullptr;
}

void retireTextureNow(stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    DeferredDeletionQueue::get().retireResource(texture);
    texture.reset();
}

void retireRenderImage(std::shared_ptr<RenderImage>& image)
{
    if (!image) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), std::move(image));
    image = nullptr;
}

void retireRenderImageNow(std::shared_ptr<RenderImage>& image)
{
    if (!image) {
        return;
    }

    DeferredDeletionQueue::get().retireResource(image);
    image.reset();
}

std::shared_ptr<IImage> getImageShared(const std::shared_ptr<RenderImage>& image, const stdptr<Texture>& texture)
{
    if (image && image->getImageShared()) {
        return image->getImageShared();
    }

    return texture ? texture->getImageShared() : nullptr;
}

IImageView* getImageView(const std::shared_ptr<RenderImage>& image, const stdptr<Texture>& texture)
{
    if (image && image->getImageView()) {
        return image->getImageView();
    }

    return texture ? texture->getImageView() : nullptr;
}

std::shared_ptr<IImageView> getImageViewShared(const std::shared_ptr<RenderImage>& image, const stdptr<Texture>& texture)
{
    if (image && image->getImageViewShared()) {
        return image->getImageViewShared();
    }

    return texture ? texture->getImageViewShared() : nullptr;
}

EFormat::T chooseSkyboxCubemapFormat(EFormat::T sourceFormat)
{
    switch (sourceFormat) {
    case EFormat::R8G8B8A8_SRGB:
        return EFormat::R8G8B8A8_SRGB;
    case EFormat::R16G16B16A16_SFLOAT:
        return EFormat::R16G16B16A16_SFLOAT;
    default:
        return EFormat::R8G8B8A8_UNORM;
    }
}

// Always use R16G16B16A16_SFLOAT for irradiance maps regardless of source format,
// because irradiance convolution accumulates many low-intensity samples and needs
// the extra precision to avoid banding artifacts.
EFormat::T chooseEnvironmentIrradianceFormat(EFormat::T /*sourceFormat*/)
{
    return EFormat::R16G16B16A16_SFLOAT;
}

// Equirectangular maps have 2:1 aspect ratio; each cube face covers 1/4 width × 1/2 height.
uint32_t computeSkyboxFaceSize(const Texture* sourceTexture)
{
    if (!sourceTexture) {
        return 0;
    }

    const auto width  = sourceTexture->getWidth();
    const auto height = sourceTexture->getHeight();
    if (width == 0 || height == 0) {
        return 0;
    }

    return std::max(1u, std::min(width / 4u, height / 2u));
}

uint32_t computeEnvironmentIrradianceFaceSize(const Texture* sourceTexture, uint32_t requestedFaceSize)
{
    if (!sourceTexture) {
        return 0;
    }

    const uint32_t maxFaceSize    = std::max(4u, requestedFaceSize);
    const uint32_t sourceFaceSize = std::max(1u, std::min(sourceTexture->getWidth(), sourceTexture->getHeight()));
    return std::max(4u, std::min(sourceFaceSize, maxFaceSize));
}

std::shared_ptr<RenderImage> createRenderableSkyboxCubemap(IRender*           render,
                                                           const std::string& label,
                                                           uint32_t           faceSize,
                                                           EFormat::T         format,
                                                           int                mips)
{
    auto* resourceFactory = render ? render->getResourceFactory() : nullptr;
    if (!resourceFactory || faceSize == 0 || format == EFormat::Undefined) {
        return nullptr;
    }

    ImageCreateInfo ci{
        .label  = std::format("{}_Image", label),
        .format = format,
        .extent = {
            .width  = faceSize,
            .height = faceSize,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = CubeFace_Count,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::ColorAttachment | EImageUsage::Sampled),
        .initialLayout = EImageLayout::Undefined,
        .flags         = EImageCreateFlag::CubeCompatible,
    };
    if (mips > 0) {
        ci.mipLevels = mips;
        ci.usage     = static_cast<EImageUsage::T>(ci.usage | EImageUsage::TransferDst | EImageUsage::TransferSrc);
    }

    auto image = resourceFactory->createImage(ci);
    if (!image) {
        return nullptr;
    }

    auto cubeView = resourceFactory->createImageView(
        image,
        ImageViewCreateInfo{
            .label       = std::format("{}_CubeView", label),
            .viewType    = EImageViewType::ViewCube,
            .aspectFlags = EImageAspect::Color,
            .baseMipLevel = 0,
            .levelCount   = ci.mipLevels,
            .baseArrayLayer = 0,
            .layerCount     = CubeFace_Count,
        });
    if (!cubeView) {
        return nullptr;
    }

    auto renderImage       = std::make_shared<RenderImage>();
    renderImage->label     = label;
    renderImage->image     = std::move(image);
    renderImage->defaultView = std::move(cubeView);
    return renderImage;
}

OffscreenJobState::CreateOutputFn makeCubemapOutputFn(const std::string& label,
                                                      uint32_t           faceSize,
                                                      EFormat::T         format,
                                                      int                mipLevels)
{
    return [label, faceSize, format, mipLevels](IRender* render) -> std::shared_ptr<RenderImage>
    {
        if (!render || label.empty() || faceSize == 0 || format == EFormat::Undefined || mipLevels <= 0) {
            return nullptr;
        }

        return createRenderableSkyboxCubemap(render, label, faceSize, format, mipLevels);
    };
}

void tryQueueJob(const std::shared_ptr<OffscreenJobState>& job)
{
    if (!job || !job->isReadyToQueue()) {
        return;
    }

    auto* const app    = App::get();
    auto* const render = app ? app->getRender() : nullptr;
    queueOffscreenJob(app, render, job);
}

} // namespace ya::detail
