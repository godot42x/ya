#include "SkyboxComponent.h"

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

    if (resolveState == ESkyboxResolveState::Dirty) {
        std::vector<std::string> facePaths(source.files.begin(), source.files.end());
        _pendingBatchLoad = std::make_shared<PendingBatchLoadState>();
        _pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
            AssetManager::TextureBatchMemoryLoadRequest{
                .filepaths = facePaths,
            });

        resolveState = ESkyboxResolveState::Resolving;
        return false;
    }

    if (resolveState == ESkyboxResolveState::Resolving) {
        AssetManager::TextureBatchMemory batchMemory;
        if (!_pendingBatchLoad ||
            !AssetManager::get()->consumeTextureBatchMemory(_pendingBatchLoad->batchHandle, batchMemory)) {
            return false;
        }

        _pendingBatchLoad.reset();

        if (batchMemory.textures.size() != CubeFace_Count || !batchMemory.isValid()) {
            cubemapTexture.reset();
            resolveState = ESkyboxResolveState::Failed;
            return false;
        }

        CubeMapMemoryCreateInfo createInfo;
        createInfo.label        = "SkyboxCubemap";
        createInfo.flipVertical = source.flipVertical;

        for (size_t index = 0; index < CubeFace_Count; ++index) {
            const auto& face = batchMemory.textures[index];
            createInfo.faces[index] = TextureMemoryView{
                .width    = face.width,
                .height   = face.height,
                .channels = face.channels,
                .format   = face.format,
                .data     = face.bytes.data(),
                .dataSize = face.bytes.size(),
            };
        }

        auto cubemap = Texture::createCubeMapFromMemory(createInfo);
        if (!cubemap || !cubemap->isValid()) {
            cubemapTexture.reset();
            resolveState = ESkyboxResolveState::Failed;
            return false;
        }

        cubemapTexture   = std::move(cubemap);
        resolveState     = ESkyboxResolveState::Ready;
        bDescriptorDirty = true;
        return true;
    }

    return resolveState == ESkyboxResolveState::Ready;
}

bool SkyboxComponent::hasRenderableCubemap() const
{
    return resolveState == ESkyboxResolveState::Ready && cubeMapDS && cubemapTexture &&
           cubemapTexture->getImageView() && !bDescriptorDirty;
}

void SkyboxComponent::invalidate()
{
    _pendingBatchLoad.reset();

    if (cubemapTexture) {
        auto& ddq = DeferredDeletionQueue::get();
        ddq.enqueueResource(ddq.currentFrame(), std::move(cubemapTexture));
        cubemapTexture = nullptr;
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
