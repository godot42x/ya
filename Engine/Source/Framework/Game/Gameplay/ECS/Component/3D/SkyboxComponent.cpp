#include "SkyboxComponent.h"
#include "Framework/Game/Resource/AssetManager.h"

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
    cubemapSource.files[static_cast<size_t>(face)] = AssetManager::normalizeAssetPath(path);
    invalidate();
}

void SkyboxComponent::setCubemapSource(const CubeMapCreateInfo& createInfo)
{
    sourceType                 = ESkyboxSourceType::CubeFaces;
    for (size_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        cubemapSource.files[faceIndex] = AssetManager::normalizeAssetPath(createInfo.files[faceIndex]);
    }
    cubemapSource.flipVertical = createInfo.flipVertical;
    invalidate();
}

void SkyboxComponent::setCylindricalSource(const std::string& filepath)
{
    sourceType                 = ESkyboxSourceType::Cylindrical;
    cylindricalSource.filepath = AssetManager::normalizeAssetPath(filepath);
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

void SkyboxComponent::invalidate()
{
    ++authoringVersion;
}

void SkyboxComponent::onPostSerialize()
{
    invalidate();
}

} // namespace ya


