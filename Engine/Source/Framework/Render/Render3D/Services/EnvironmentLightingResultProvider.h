#pragma once

#include "Render3D/EnvironmentLighting/EnvironmentLightingProcessor.h"

#include <functional>

namespace ya
{

struct Scene;

/// Narrow read-only environment-lighting result provider, injected by the
/// Host composition. Render3D consumers read derived-resource handles through
/// this contract and never locate the processor via the App singleton.
struct EnvironmentLightingResultProvider
{
    std::function<std::shared_ptr<ImageResource>(Scene*)>              resolveSceneSkyboxResource;
    std::function<EnvironmentLightingSceneResources(Scene*)>           resolveSceneEnvironmentLightingResources;

    [[nodiscard]] bool isBound() const
    {
        return static_cast<bool>(resolveSceneSkyboxResource) ||
               static_cast<bool>(resolveSceneEnvironmentLightingResources);
    }
};

} // namespace ya
