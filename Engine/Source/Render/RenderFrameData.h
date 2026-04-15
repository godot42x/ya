#pragma once

#include "Render/Core/Texture.h"
#include "Render/Material/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderDefines.h"

#include <glm/glm.hpp>
#include <vector>

namespace ya
{

/// A single renderable instance snapshot — everything the draw call needs.
struct RenderDrawItem
{
    glm::mat4 worldMatrix;     // from TransformComponent::getTransform()
    Mesh*     mesh;            // raw pointer: Mesh lifetime is managed by AssetManager
    Material* material;        // raw pointer: Material lifetime is managed by MaterialFactory
    uint32_t  materialIndex;   // material->getIndex(), used for descriptor set lookup
    float     sortKey;         // distance to camera (or other sort criterion)
};

/// Skybox / Environment snapshot — textures only, no descriptor sets.
struct RenderSkyboxData
{
    stdptr<Texture> cubemapTexture;
    stdptr<Texture> irradianceTexture;
};

/// All data a render pipeline needs for one frame.
/// Built once per frame from the ECS registry, then consumed read-only by every pipeline / system.
struct RenderFrameData
{
    // ═══════════════════════════════════════════════════════════════
    // View / Camera
    // ═══════════════════════════════════════════════════════════════
    glm::mat4    view           = glm::mat4(1.0f);
    glm::mat4    projection     = glm::mat4(1.0f);
    glm::vec3    cameraPos      = glm::vec3(0.0f);
    Extent2D     viewportExtent = {};
    entt::entity viewOwner      = entt::null;

    // ═══════════════════════════════════════════════════════════════
    // Lights (reuses FrameContext sub-structures)
    // ═══════════════════════════════════════════════════════════════
    bool                                                       bHasDirectionalLight = false;
    FrameContext::DirectionalLightData                          directionalLight;
    uint32_t                                                   numPointLights = 0;
    std::array<FrameContext::PointLightData, MAX_POINT_LIGHTS> pointLights;

    // ═══════════════════════════════════════════════════════════════
    // Skybox / Environment
    // ═══════════════════════════════════════════════════════════════
    RenderSkyboxData skybox;

    // ═══════════════════════════════════════════════════════════════
    // Draw lists (bucketed by shading model)
    // ═══════════════════════════════════════════════════════════════
    std::vector<RenderDrawItem> pbrDrawItems;
    std::vector<RenderDrawItem> phongDrawItems;
    std::vector<RenderDrawItem> unlitDrawItems;
    std::vector<RenderDrawItem> simpleDrawItems;
    std::vector<RenderDrawItem> fallbackDrawItems; // meshes without a material

    // ═══════════════════════════════════════════════════════════════
    // Frame constants
    // ═══════════════════════════════════════════════════════════════
    uint64_t frameIndex = 0;
    float    deltaTime  = 0.0f;

    // ═══════════════════════════════════════════════════════════════
    // Helpers
    // ═══════════════════════════════════════════════════════════════
    void clear()
    {
        pbrDrawItems.clear();
        phongDrawItems.clear();
        unlitDrawItems.clear();
        simpleDrawItems.clear();
        fallbackDrawItems.clear();
        skybox = {};
    }

    /// Build a backward-compatible FrameContext for systems that haven't migrated yet.
    [[nodiscard]] FrameContext toFrameContext() const
    {
        FrameContext ctx;
        ctx.view                 = view;
        ctx.projection           = projection;
        ctx.cameraPos            = cameraPos;
        ctx.bHasDirectionalLight = bHasDirectionalLight;
        ctx.directionalLight     = directionalLight;
        ctx.numPointLights       = numPointLights;
        ctx.pointLights          = pointLights;
        ctx.viewOwner            = viewOwner;
        ctx.extent               = viewportExtent;
        return ctx;
    }

    [[nodiscard]] size_t totalDrawCount() const
    {
        return pbrDrawItems.size() + phongDrawItems.size() +
               unlitDrawItems.size() + simpleDrawItems.size() +
               fallbackDrawItems.size();
    }
};

} // namespace ya
