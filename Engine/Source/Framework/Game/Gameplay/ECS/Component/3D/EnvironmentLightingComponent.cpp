#include "EnvironmentLightingComponent.h"
#include "Framework/Game/Resource/AssetManager.h"

namespace ya
{

namespace
{

bool isEnvironmentLightingSourceStateLoading(EEnvironmentLightingSourceResolveState state)
{
    return state == EEnvironmentLightingSourceResolveState::ResolvingSource ||
           state == EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap;
}

bool isEnvironmentLightingIrradianceStateLoading(EEnvironmentLightingIrradianceResolveState state)
{
    return state == EEnvironmentLightingIrradianceResolveState::Building;
}

bool isEnvironmentLightingPrefilterStateLoading(EEnvironmentLightingPrefilterResolveState state)
{
    return state == EEnvironmentLightingPrefilterResolveState::Building;
}

} // namespace

bool EnvironmentLightingComponent::CubemapSource::hasAllFaces() const
{
    for (const auto& file : files) {
        if (file.empty()) {
            return false;
        }
    }
    return true;
}

bool EnvironmentLightingComponent::CylindricalSource::hasFile() const
{
    return !filepath.empty();
}

void EnvironmentLightingComponent::setFace(ECubeFace face, const std::string& path)
{
    if (face < CubeFace_PosX || face >= CubeFace_Count) {
        return;
    }

    sourceType                                     = EEnvironmentLightingSourceType::CubeFaces;
    cubemapSource.files[static_cast<size_t>(face)] = AssetManager::normalizeAssetPath(path);
    invalidate();
}

void EnvironmentLightingComponent::setCubemapSource(const CubeMapCreateInfo& createInfo)
{
    sourceType                 = EEnvironmentLightingSourceType::CubeFaces;
    for (size_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        cubemapSource.files[faceIndex] = AssetManager::normalizeAssetPath(createInfo.files[faceIndex]);
    }
    cubemapSource.flipVertical = createInfo.flipVertical;
    invalidate();
}

void EnvironmentLightingComponent::setCylindricalSource(const std::string& filepath)
{
    sourceType                 = EEnvironmentLightingSourceType::Cylindrical;
    cylindricalSource.filepath = AssetManager::normalizeAssetPath(filepath);
    invalidate();
}

bool EnvironmentLightingComponent::usesSceneSkybox() const
{
    return sourceType == EEnvironmentLightingSourceType::SceneSkybox;
}

bool EnvironmentLightingComponent::hasCylindricalSource() const
{
    return sourceType == EEnvironmentLightingSourceType::Cylindrical && cylindricalSource.hasFile();
}

void EnvironmentLightingComponent::invalidate()
{
    ++authoringVersion;
}

uint32_t EnvironmentLightingComponent::getResolvedIrradianceFaceSize() const
{
    return std::max(4u, irradianceFaceSize);
}

void EnvironmentLightingComponent::onPostSerialize()
{
    invalidate();
}

} // namespace ya

