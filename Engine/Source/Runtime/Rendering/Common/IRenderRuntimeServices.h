#pragma once

#include "Render/Core/DescriptorSet.h"
#include "ECS/System/ResourceResolveSystem.h"

#include <cstdint>

namespace ya
{

struct Scene;
struct DebugRenderSystem;

struct IRenderRuntimeServices
{
    virtual ~IRenderRuntimeServices() = default;

    [[nodiscard]] virtual uint64_t                         getFrameIndex() const = 0;
    [[nodiscard]] virtual double                           getElapsedTimeSeconds() const = 0;
    [[nodiscard]] virtual Scene*                           getActiveScene() const = 0;
    [[nodiscard]] virtual ResourceResolveSystem*           getResourceResolveSystem() const = 0;
    [[nodiscard]] virtual DescriptorSetHandle              getSceneSkyboxDescriptorSet(Scene* scene = nullptr) = 0;
    [[nodiscard]] virtual DescriptorSetHandle              getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr) = 0;
    [[nodiscard]] virtual EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const = 0;
    [[nodiscard]] virtual DebugRenderSystem&               getDebugRenderSystem() const = 0;
};

} // namespace ya
