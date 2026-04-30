#pragma once

#include "Render/Core/Texture.h"
#include "Render/Material/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderDefines.h"
#include "Common.Limits.slang.h"

#include <glm/glm.hpp>
#include <array>
#include <vector>

namespace ya
{

using slang_types::Common::Limits::MAX_BONE_COUNT;

struct RenderSkinningPalette
{
    std::array<glm::mat4, MAX_BONE_COUNT> boneMatrices{};

    RenderSkinningPalette()
    {
        boneMatrices.fill(glm::mat4(1.0f));
    }
};

/// A single renderable instance snapshot — everything the draw call needs.
struct RenderDrawItem
{
    glm::mat4 worldMatrix;     // from TransformComponent::getTransform()
    Mesh*     mesh;            // raw pointer: Mesh lifetime is managed by AssetManager
    Material* material;        // raw pointer: Material lifetime is managed by MaterialFactory
    uint32_t  materialIndex;   // material->getIndex(), used for descriptor set lookup
    float     sortKey;         // distance to camera (or other sort criterion)
    int32_t   skinningPaletteIndex = -1; // -1 means static draw, otherwise index into RenderFrameData::skinningPalettes
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

    // Animation / Skinning snapshot data.
    std::vector<RenderSkinningPalette> skinningPalettes;

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
        skinningPalettes.clear();
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
