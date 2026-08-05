#pragma once

#include "Render/Material/Material.h"
#include "Render/Mesh.h"
#include "Render/RenderDefines.h"
#include "Common.Limits.slang.h"

#include <glm/glm.hpp>
#include <array>
#include <span>
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

/// Read-only view over extracted draw candidates.
///
/// The view wraps the existing RenderDrawItem snapshot instead of introducing
/// another ownership or shader-facing representation.
class DrawCandidateView
{
  public:
    using value_type     = RenderDrawItem;
    using const_iterator = std::span<const value_type>::iterator;

    DrawCandidateView() = default;

    explicit DrawCandidateView(std::span<const value_type> candidates)
        : _candidates(candidates)
    {}

    [[nodiscard]] const value_type* data() const { return _candidates.data(); }
    [[nodiscard]] size_t            size() const { return _candidates.size(); }
    [[nodiscard]] bool              empty() const { return _candidates.empty(); }

    [[nodiscard]] const value_type& operator[](size_t index) const
    {
        return _candidates[index];
    }

    [[nodiscard]] const_iterator begin() const { return _candidates.begin(); }
    [[nodiscard]] const_iterator end() const { return _candidates.end(); }

  private:
    std::span<const value_type> _candidates{};
};

struct RenderShadingDrawBuckets
{
    std::vector<RenderDrawItem> pbrDrawItems;
    std::vector<RenderDrawItem> phongDrawItems;
    std::vector<RenderDrawItem> unlitDrawItems;
    std::vector<RenderDrawItem> simpleDrawItems;
    std::vector<RenderDrawItem> fallbackDrawItems;

    void clear()
    {
        pbrDrawItems.clear();
        phongDrawItems.clear();
        unlitDrawItems.clear();
        simpleDrawItems.clear();
        fallbackDrawItems.clear();
    }

    [[nodiscard]] size_t totalDrawCount() const
    {
        return pbrDrawItems.size() + phongDrawItems.size() +
               unlitDrawItems.size() + simpleDrawItems.size() +
               fallbackDrawItems.size();
    }
};

struct RenderMeshClassDrawBuckets
{
    RenderShadingDrawBuckets staticMeshes;
    RenderShadingDrawBuckets skinnedMeshes;

    void clear()
    {
        staticMeshes.clear();
        skinnedMeshes.clear();
    }

    [[nodiscard]] size_t totalDrawCount() const
    {
        return staticMeshes.totalDrawCount() + skinnedMeshes.totalDrawCount();
    }
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
    // Draw lists (bucketed by mesh class, then shading model)
    // ═══════════════════════════════════════════════════════════════
    RenderMeshClassDrawBuckets drawBuckets;

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
        drawBuckets.clear();
        skinningPalettes.clear();
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
        return drawBuckets.totalDrawCount();
    }
};

} // namespace ya
