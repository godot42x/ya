
#pragma once

#include "Core/Base.h"
#include "Core/System/System.h"

namespace ya
{

/**
 * @brief ResourceResolveSystem - Unified resource loading system
 *
 * Handles all resource resolution in one place:
 * - MeshComponent: Loads primitive geometry or external models
 * - IMaterialComponent: Loads textures and creates runtime materials
 *
 * Call order during frame:
 * 1. ResourceResolveSystem::onUpdate() - Load resources
 * 2. MaterialSystem::onUpdateByRenderTarget() - Prepare descriptors
 * 3. MaterialSystem::onRender() - Render
 */
struct ResourceResolveSystem : public ISystem
{
    void init() {}

    /**
     * @brief Resolve all pending resources
     * Iterates through components and calls resolve() on unresolved ones
     */
    void onUpdate(float dt) override;

    void destroy() {}
};

} // namespace ya