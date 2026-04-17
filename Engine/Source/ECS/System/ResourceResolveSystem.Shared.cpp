#include "ResourceResolveSystem.Detail.h"

#include "Render/Core/TextureFactory.h"
#include "Resource/DeferredDeletionQueue.h"
#include "Runtime/App/App.h"
#include "Runtime/App/OffscreenJobRunner.h"


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

EFormat::T chooseEnvironmentIrradianceFormat(EFormat::T sourceFormat)
{
    (void)sourceFormat;
    return EFormat::R16G16B16A16_SFLOAT;
}

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

stdptr<Texture> createRenderableSkyboxCubemap(IRender*           render,
                                              const std::string& label,
                                              uint32_t           faceSize,
                                              EFormat::T         format,
                                              int                mips)
{
    auto* textureFactory = render ? render->getTextureFactory() : nullptr;
    if (!textureFactory || faceSize == 0 || format == EFormat::Undefined) {
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

    auto image = textureFactory->createImage(ci);
    if (!image) {
        return nullptr;
    }

    auto cubeView = textureFactory->createCubeMapImageView(image, EImageAspect::Color, 0, ci.mipLevels);
    if (!cubeView) {
        return nullptr;
    }

    return Texture::wrap(image, cubeView, label);
}

OffscreenJobState::CreateOutputFn makeCubemapOutputFn(const std::string& label,
                                                      uint32_t           faceSize,
                                                      EFormat::T         format,
                                                      int                mipLevels)
{
    return [label, faceSize, format, mipLevels](IRender* render) -> stdptr<Texture>
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