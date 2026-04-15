#include "EnvironmentLightingComponent.h"

namespace ya
{

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
    cubemapSource.files[static_cast<size_t>(face)] = path;
    invalidate();
}

void EnvironmentLightingComponent::setCubemapSource(const CubeMapCreateInfo& createInfo)
{
    sourceType                 = EEnvironmentLightingSourceType::CubeFaces;
    cubemapSource.files        = createInfo.files;
    cubemapSource.flipVertical = createInfo.flipVertical;
    invalidate();
}

void EnvironmentLightingComponent::setCylindricalSource(const std::string& filepath)
{
    sourceType                 = EEnvironmentLightingSourceType::Cylindrical;
    cylindricalSource.filepath = filepath;
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
    auto transition = makeTransition(resolveState, "EnvironmentLighting");
    transition.to(hasSource() ? EEnvironmentLightingResolveState::Dirty : EEnvironmentLightingResolveState::Empty,
                  "invalidate");
}

bool EnvironmentLightingComponent::isLoading() const
{
    return resolveState == EEnvironmentLightingResolveState::ResolvingSource ||
           resolveState == EEnvironmentLightingResolveState::PreprocessingEnvironment ||
           resolveState == EEnvironmentLightingResolveState::PreprocessingIrradiance;
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