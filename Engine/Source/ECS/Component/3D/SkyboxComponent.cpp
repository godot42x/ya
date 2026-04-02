#include "SkyboxComponent.h"

#include "Resource/AssetManager.h"
#include "Resource/DeferredDeletionQueue.h"

namespace ya
{

bool SkyboxComponent::CubemapSource::hasAllFaces() const
{
    for (const auto& file : files) {
        if (file.empty()) {
            return false;
        }
    }
    return true;
}

void SkyboxComponent::setFace(ECubeFace face, const std::string& path)
{
    if (face < CubeFace_PosX || face >= CubeFace_Count) {
        return;
    }

    source.files[static_cast<size_t>(face)] = path;
    invalidate();
}

void SkyboxComponent::setCubemapSource(const CubeMapCreateInfo& createInfo)
{
    source.files        = createInfo.files;
    source.flipVertical = createInfo.flipVertical;
    invalidate();
}

bool SkyboxComponent::hasCubemapSource() const
{
    return source.hasAllFaces();
}

bool SkyboxComponent::resolve()
{
    if (!hasCubemapSource()) {
        cubemapTexture.reset();
        resolveState     = ESkyboxResolveState::Empty;
        bDescriptorDirty = false;
        return false;
    }

    bool allFacesReady = true;
    for (const auto& file : source.files) {
        auto future = AssetManager::get()->loadTexture(file, AssetManager::ETextureColorSpace::SRGB);
        if (!future.isReady()) {
            allFacesReady = false;
        }
    }

    if (!allFacesReady) {
        resolveState = ESkyboxResolveState::Resolving;
        return false;
    }

    CubeMapCreateInfo createInfo{
        .label        = "SkyboxCubemap",
        .files        = source.files,
        .flipVertical = source.flipVertical,
    };

    cubemapTexture = Texture::createCubeMap(createInfo);
    if (!cubemapTexture || !cubemapTexture->isValid()) {
        cubemapTexture.reset();
        resolveState = ESkyboxResolveState::Failed;
        return false;
    }

    resolveState     = ESkyboxResolveState::Ready;
    bDescriptorDirty = true;
    return true;
}

bool SkyboxComponent::hasRenderableCubemap() const
{
    return resolveState == ESkyboxResolveState::Ready && cubeMapDS && cubemapTexture &&
           cubemapTexture->getImageView() && !bDescriptorDirty;
}

void SkyboxComponent::invalidate()
{
    if (cubemapTexture) {
        // GPU may still be reading this texture in in-flight frames.
        // Hand ownership to the deferred deletion queue; it will call
        // cubemapTexture.reset() only after all in-flight frames have finished.
        auto& ddq = DeferredDeletionQueue::get();
        ddq.enqueueResource(ddq.currentFrame(), std::move(cubemapTexture));
        cubemapTexture = nullptr; // move already cleared it, but be explicit
    }
    resolveState     = hasCubemapSource() ? ESkyboxResolveState::Dirty : ESkyboxResolveState::Empty;
    bDescriptorDirty = true;
}

bool SkyboxComponent::isLoading() const
{
    return resolveState == ESkyboxResolveState::Resolving;
}

void SkyboxComponent::onPostSerialize()
{
    invalidate();
}


} // namespace ya
