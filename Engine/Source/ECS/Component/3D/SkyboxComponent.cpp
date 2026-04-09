#include "SkyboxComponent.h"

#include "Render/Core/TextureFactory.h"
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

bool SkyboxComponent::hasRenderableCubemap() const
{
    return resolveState == ESkyboxResolveState::Ready && cubemapTexture && cubemapTexture->getImageView();
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

void SkyboxComponent::prepareForRemove()
{
    sourcePreviewTexture.reset();
    clearCubemapPreviewViews();

    if (cubemapTexture) {
        auto& ddq = DeferredDeletionQueue::get();
        ddq.enqueueResource(ddq.currentFrame(), std::move(cubemapTexture));
        cubemapTexture = nullptr;
    }
}

void SkyboxComponent::invalidate()
{
    prepareForRemove();
    auto transition = makeTransition(resolveState, "Skybox");
    transition.to(hasSource() ? ESkyboxResolveState::Dirty : ESkyboxResolveState::Empty,
                  "invalidate");
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
