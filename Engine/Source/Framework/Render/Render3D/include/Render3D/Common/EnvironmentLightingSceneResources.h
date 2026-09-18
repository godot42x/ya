#pragma once

#include "RHI/Core/ImageResource.h"
#include "RHI/Core/RenderTexture.h"

#include <memory>

namespace ya
{

/// One Scene's derived environment-lighting images, as a value.
///
/// This is a *result* type, not processor state: it is what a resolver hands
/// back before graph build so a pass can bind it without locating the processor
/// or the App. It lives on its own so that naming it does not pull in the
/// whole EnvironmentLightingProcessor (ECS components, cubemap pipelines).
struct EnvironmentLightingSceneResources
{
    std::shared_ptr<ImageResource> cubemap    = nullptr;
    std::shared_ptr<ImageResource> irradiance = nullptr;
    std::shared_ptr<ImageResource> prefilter  = nullptr;
    std::shared_ptr<RenderTexture> brdfLut    = nullptr;

    [[nodiscard]] bool isComplete() const
    {
        return cubemap && cubemap->isValid() && irradiance && irradiance->isValid() && prefilter && prefilter->isValid() &&
               brdfLut && brdfLut->getImageView();
    }
};

} // namespace ya
