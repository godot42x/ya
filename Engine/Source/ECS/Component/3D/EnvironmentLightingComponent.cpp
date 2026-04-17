#include "EnvironmentLightingComponent.h"

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
    const bool bHasSource = hasSource();

    makeTransition(sourceState, "EnvironmentLighting.Source")
        .to(bHasSource ? EEnvironmentLightingSourceResolveState::Dirty : EEnvironmentLightingSourceResolveState::Empty,
            "invalidate");
    makeTransition(irradianceState, "EnvironmentLighting.Irradiance")
        .to(bEnableIrradiance ? (bHasSource ? EEnvironmentLightingIrradianceResolveState::Dirty
                                            : EEnvironmentLightingIrradianceResolveState::Empty)
                              : EEnvironmentLightingIrradianceResolveState::Disabled,
            "invalidate");
    makeTransition(prefilterState, "EnvironmentLighting.Prefilter")
        .to(bEnablePrefilter ? (bHasSource ? EEnvironmentLightingPrefilterResolveState::Dirty
                                           : EEnvironmentLightingPrefilterResolveState::Empty)
                             : EEnvironmentLightingPrefilterResolveState::Disabled,
            "invalidate");
}

bool EnvironmentLightingComponent::isLoading() const
{
    return isEnvironmentLightingSourceStateLoading(sourceState) ||
           isEnvironmentLightingIrradianceStateLoading(irradianceState) ||
           isEnvironmentLightingPrefilterStateLoading(prefilterState);
}

bool EnvironmentLightingComponent::hasReadySource() const
{
    return sourceState == EEnvironmentLightingSourceResolveState::Ready;
}

bool EnvironmentLightingComponent::hasReadyIrradiance() const
{
    return bEnableIrradiance && irradianceState == EEnvironmentLightingIrradianceResolveState::Ready;
}

bool EnvironmentLightingComponent::hasReadyPrefilter() const
{
    return bEnablePrefilter && prefilterState == EEnvironmentLightingPrefilterResolveState::Ready;
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