#include "SkyboxComponent.h"

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

void SkyboxComponent::invalidate()
{
    ++authoringVersion;
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
