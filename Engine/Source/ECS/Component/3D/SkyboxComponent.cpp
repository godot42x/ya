#include "SkyboxComponent.h"

#include "Render/Core/TextureFactory.h"
#include "Resource/DeferredDeletionQueue.h"

namespace ya
{

namespace
{
void retireTextureForDeferredDeletion(const stdptr<Texture>& texture)
{
    if (!texture) {
        return;
    }

    auto& ddq = DeferredDeletionQueue::get();
    ddq.enqueueResource(ddq.currentFrame(), texture);
}
} // namespace

bool SkyboxComponent::CubemapSource::hasAllFaces() const
{
    for (const auto& file : files) {
        if (file.empty()) {
            return false;
        }
    }
    return true;
}

bool SkyboxComponent::CylindricalSource::hasFile() const
{
    return !filepath.empty();
}

void SkyboxComponent::setFace(ECubeFace face, const std::string& path)
{
    if (face < CubeFace_PosX || face >= CubeFace_Count) {
        return;
    }

    sourceType                                     = ESkyboxSourceType::CubeFaces;
    cubemapSource.files[static_cast<size_t>(face)] = path;
    invalidate();
}

void SkyboxComponent::setCubemapSource(const CubeMapCreateInfo& createInfo)
{
    sourceType                 = ESkyboxSourceType::CubeFaces;
    cubemapSource.files        = createInfo.files;
    cubemapSource.flipVertical = createInfo.flipVertical;
    invalidate();
}

void SkyboxComponent::setCylindricalSource(const std::string& filepath)
{
    sourceType                 = ESkyboxSourceType::Cylindrical;
    cylindricalSource.filepath = filepath;
    invalidate();
}

bool SkyboxComponent::hasSource() const
{
    if (sourceType == ESkyboxSourceType::CubeFaces) {
        return hasCubemapSource();
    }
    return hasCylindricalSource();
}

bool SkyboxComponent::hasCubemapSource() const
{
    return sourceType == ESkyboxSourceType::CubeFaces && cubemapSource.hasAllFaces();
}

bool SkyboxComponent::hasCylindricalSource() const
{
    return sourceType == ESkyboxSourceType::Cylindrical && cylindricalSource.hasFile();
}

bool SkyboxComponent::resolve()
{
    if (!hasSource()) {
        cubemapTexture.reset();
        sourcePreviewTexture.reset();
        clearCubemapPreviewViews();
        resolveState     = ESkyboxResolveState::Empty;
        bDescriptorDirty = false;
        return false;
    }

    if (resolveState == ESkyboxResolveState::Dirty) {
        if (hasCubemapSource()) {
            std::vector<std::string> facePaths(cubemapSource.files.begin(), cubemapSource.files.end());
            _pendingBatchLoad              = std::make_shared<PendingBatchLoadState>();
            _pendingBatchLoad->batchHandle = AssetManager::get()->loadTextureBatchIntoMemory(
                AssetManager::TextureBatchMemoryLoadRequest{
                    .filepaths = facePaths,
                });
        }
        resolveState = ESkyboxResolveState::ResolvingSource;
        return false;
    }

    if (hasCubemapSource() && resolveState == ESkyboxResolveState::ResolvingSource) {
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
        createInfo.flipVertical = cubemapSource.flipVertical;

        for (size_t index = 0; index < CubeFace_Count; ++index) {
            const auto& face        = batchMemory.textures[index];
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
            clearCubemapPreviewViews();
            resolveState = ESkyboxResolveState::Failed;
            return false;
        }

        cubemapTexture   = std::move(cubemap);
        rebuildCubemapPreviewViews();
        resolveState     = ESkyboxResolveState::Ready;
        bDescriptorDirty = true;
        return true;
    }

    if (hasCylindricalSource() && resolveState == ESkyboxResolveState::ResolvingSource) {
        const auto future = AssetManager::get()->loadTexture(
            AssetManager::TextureLoadRequest{
                .filepath        = cylindricalSource.filepath,
                .name            = "SkyboxCylindricalSource",
                .onReady         = {},
                .colorSpace      = AssetManager::ETextureColorSpace::SRGB,
                .textureSemantic = std::nullopt,
            });
        if (!future.isReady()) {
            return false;
        }

        auto sourceTexture = future.getShared();
        if (!sourceTexture || !sourceTexture->getImageView()) {
            resolveState = ESkyboxResolveState::Failed;
            return false;
        }

        sourcePreviewTexture                     = sourceTexture;
        _pendingOffscreenProcess                = std::make_shared<PendingOffscreenProcessState>();
        _pendingOffscreenProcess->sourceTexture = std::move(sourceTexture);
        _pendingOffscreenProcess->bFlipVertical = cylindricalSource.flipVertical;
        resolveState                            = ESkyboxResolveState::Preprocessing;
        return false;
    }

    if (hasCylindricalSource() && resolveState == ESkyboxResolveState::Preprocessing) {
        if (!_pendingOffscreenProcess || !_pendingOffscreenProcess->bTaskFinished) {
            return false;
        }

        if (!_pendingOffscreenProcess->bTaskSucceeded || !_pendingOffscreenProcess->outputTexture) {
            _pendingOffscreenProcess.reset();
            cubemapTexture.reset();
            clearCubemapPreviewViews();
            resolveState = ESkyboxResolveState::Failed;
            return false;
        }

        cubemapTexture = std::move(_pendingOffscreenProcess->outputTexture);
        rebuildCubemapPreviewViews();
        _pendingOffscreenProcess.reset();
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

void SkyboxComponent::rebuildCubemapPreviewViews()
{
    clearCubemapPreviewViews();
    if (!cubemapTexture || !cubemapTexture->getImageShared() || !cubemapTexture->getImageView()) {
        return;
    }

    auto* textureFactory = ITextureFactory::get();
    if (!textureFactory) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        cubemapFacePreviewViews[faceIndex] = textureFactory->createImageView(
            cubemapTexture->getImageShared(),
            ImageViewCreateInfo{
                .label          = std::format("SkyboxPreviewFace{}", faceIndex),
                .viewType       = EImageViewType::View2D,
                .aspectFlags    = EImageAspect::Color,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = faceIndex,
                .layerCount     = 1,
            });
    }
}

void SkyboxComponent::clearCubemapPreviewViews()
{
    for (auto& faceView : cubemapFacePreviewViews) {
        faceView.reset();
    }
}

IImageView* SkyboxComponent::getCubemapFacePreviewView(uint32_t faceIndex) const
{
    if (faceIndex >= CubeFace_Count || !cubemapFacePreviewViews[faceIndex]) {
        return nullptr;
    }
    return cubemapFacePreviewViews[faceIndex].get();
}

void SkyboxComponent::invalidate()
{
    _pendingBatchLoad.reset();
    if (_pendingOffscreenProcess) {
        _pendingOffscreenProcess->bCancelled = true;
        retireTextureForDeferredDeletion(_pendingOffscreenProcess->outputTexture);
        _pendingOffscreenProcess->outputTexture.reset();
        _pendingOffscreenProcess.reset();
    }
    sourcePreviewTexture.reset();
    clearCubemapPreviewViews();

    if (cubemapTexture) {
        auto& ddq = DeferredDeletionQueue::get();
        ddq.enqueueResource(ddq.currentFrame(), std::move(cubemapTexture));
        cubemapTexture = nullptr;
    }
    resolveState     = hasSource() ? ESkyboxResolveState::Dirty : ESkyboxResolveState::Empty;
    bDescriptorDirty = true;
}

bool SkyboxComponent::isLoading() const
{
    return resolveState == ESkyboxResolveState::ResolvingSource ||
           resolveState == ESkyboxResolveState::Preprocessing;
}

void SkyboxComponent::onPostSerialize()
{
    invalidate();
}

} // namespace ya
