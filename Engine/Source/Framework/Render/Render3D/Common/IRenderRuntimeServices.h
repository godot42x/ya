#pragma once

#include "RHI/Core/DescriptorSet.h"

#include <cstdint>

namespace ya
{

struct Scene;
struct DebugRenderSystem;
struct GameplayResourceBinding;
struct EnvironmentLightingProcessor;
struct EnvironmentLightingSceneResources;

struct IRenderRuntimeServices
{
    virtual ~IRenderRuntimeServices() = default;

    [[nodiscard]] virtual uint64_t                         getFrameIndex() const = 0;
    [[nodiscard]] virtual double                           getElapsedTimeSeconds() const = 0;
    [[nodiscard]] virtual Scene*                           getActiveScene() const = 0;
    [[nodiscard]] virtual GameplayResourceBinding*           getGameplayResourceBinding() const = 0;
    [[nodiscard]] virtual EnvironmentLightingProcessor*    getEnvironmentLightingProcessor() const = 0;
    [[nodiscard]] virtual DescriptorSetHandle              getSceneSkyboxDescriptorSet(Scene* scene = nullptr) = 0;
    [[nodiscard]] virtual DescriptorSetHandle              getSceneEnvironmentLightingDescriptorSet(Scene* scene = nullptr) = 0;
    [[nodiscard]] virtual EnvironmentLightingSceneResources resolveSceneEnvironmentLightingResources(Scene* scene = nullptr) const = 0;
    [[nodiscard]] virtual DebugRenderSystem&               getDebugRenderSystem() const = 0;
};

} // namespace ya
