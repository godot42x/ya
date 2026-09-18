#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "Render3D/Common/EnvironmentLightingSceneResources.h"

namespace ya
{

struct EnvironmentLightingProcessor;

/// The Scene-keyed GPU bindings one View's passes bind while recording.
///
/// A View's Scene is known from its declaration, so these are resolved before
/// the graph is built and carried on the View's prepared data. Recording then
/// reads the View it is drawing instead of asking an owner "which Scene is
/// current now" -- the query that made the same pass produce different results
/// depending on when it ran.
struct RenderViewSceneResources
{
    /// Derived environment-lighting state for that Scene (skybox/IBL lookup).
    /// Null when the View's Scene has no such state or none was resolved.
    EnvironmentLightingProcessor*     environmentLighting = nullptr;
    EnvironmentLightingSceneResources environmentLightingResources{};
    /// Bound IBL / skybox descriptor sets, or empty handles.
    DescriptorSetHandle               skyboxDescriptorSet{};
    DescriptorSetHandle               environmentLightingDescriptorSet{};

    [[nodiscard]] bool hasEnvironmentLighting() const { return environmentLighting != nullptr; }
    [[nodiscard]] bool hasSkybox() const { return static_cast<bool>(skyboxDescriptorSet); }

    void clear()
    {
        environmentLighting = nullptr;
        environmentLightingResources = {};
        skyboxDescriptorSet = {};
        environmentLightingDescriptorSet = {};
    }
};

} // namespace ya
