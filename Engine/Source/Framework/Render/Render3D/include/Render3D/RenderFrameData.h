#pragma once

#include "Render3D/Material/Material.h"
#include "Render3D/Common/RenderFeatures.h"
#include "Render3D/Common/RenderViewSceneResources.h"
#include "Resource/Mesh.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"
#include "Common.Limits.slang.h"

#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <iterator>
#include <numeric>
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
    uint32_t  entityId  = 0;   // raw entt entity handle, written by the entity-id pick pass
    float     sortKey;         // distance to camera (or other sort criterion)
    int32_t   skinningPaletteIndex = -1; // -1 means static draw, otherwise index into RenderFrameData::skinningPalettes
    /// Which views may draw this item. Authored content is `Game`; a generated
    /// editor companion is `Gizmo`, so a game view drops it without the owning
    /// component having to know about views at all.
    FRenderFeatureMask features = toMask(ERenderFeature::Game);
    /// Host entity when this item belongs to a generated companion; 0 for
    /// authored entities. A camera's own preview view drops its own body.
    uint32_t  hostEntityId = 0;
};

/// One authored scene sprite, ready for the scene's sprite pass.
///
/// Extracted once per (Scene, sceneRevision) and shared by every View, so it
/// holds no camera state: which sprites a View sees and in which order is the
/// View's own bucket (ViewSpriteBucket). The component's transform is already
/// folded into the world axes, so expanding the quad needs no view matrix, and
/// the resolved texture binding keeps the GPU texture alive for as long as the
/// candidate exists -- a recording may not reference a texture that dies before
/// its submission completed.
struct WorldSpriteCandidate
{
    /// Quad centre in world space. With the default pivot this is the entity
    /// position; any other pivot shifts it so that point stays on the entity.
    glm::vec3 worldCenter = glm::vec3(0.0f);
    /// World axes scaled by the authored size: the quad corner at quad-space
    /// (cx, cy) sits at `worldCenter + axisX * cx + axisY * cy`, with cx and cy
    /// in [-0.5, 0.5]. Projection is what makes a far sprite look smaller; the
    /// quad itself is never resized from camera distance or FOV.
    glm::vec3 axisX = glm::vec3(0.5f, 0.0f, 0.0f);
    glm::vec3 axisY = glm::vec3(0.0f, 0.5f, 0.0f);
    /// Atlas window (u0, v0, u1, v1) with the component's flip flags applied.
    glm::vec4 uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec4 tint   = glm::vec4(1.0f);
    /// Resolved texture + sampler. Only drawable sprites are extracted, so this
    /// is never the "still loading" empty binding.
    TextureBinding texture{};
    uint32_t       entityId  = 0;
    int32_t        layer     = 0;
    int32_t        sortOrder = 0;
    /// `tint.a` < 1: the sprite blends, so it must not write depth (see the
    /// component's draw policy). The pass picks its pipeline from this.
    bool bTranslucent = false;
    /// Which views may draw this sprite; a generated companion would take its
    /// host's set, exactly like RenderDrawItem::features.
    FRenderFeatureMask features = toMask(ERenderFeature::Game);
    /// Host entity when this candidate is a generated companion; 0 for authored
    /// sprites.
    uint32_t hostEntityId = 0;
};

/// Read-only view over extracted scene candidates.
///
/// The view wraps an existing immutable candidate vector instead of introducing
/// another ownership or shader-facing representation: the Scene owns the
/// storage, only the camera-dependent order belongs to the View.
template <typename Candidate>
class CandidateOrderView
{
  public:
    using value_type     = Candidate;
    class const_iterator
    {
      public:
        using difference_type   = std::ptrdiff_t;
        using value_type        = const Candidate;
        using pointer           = const Candidate*;
        using reference         = const Candidate&;
        using iterator_concept  = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;

        const_iterator() = default;
        const_iterator(std::span<const Candidate> candidates,
                       std::span<const uint32_t>  order,
                       size_t                     position,
                       bool                       indexed)
            : _candidates(candidates), _order(order), _position(position), _indexed(indexed)
        {}

        reference operator*() const { return _candidates[_indexed ? _order[_position] : _position]; }
        pointer   operator->() const { return &operator*(); }
        const_iterator& operator++()
        {
            ++_position;
            return *this;
        }
        const_iterator operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }
        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs)
        {
            return lhs._position == rhs._position && lhs._candidates.data() == rhs._candidates.data() &&
                   lhs._order.data() == rhs._order.data() && lhs._indexed == rhs._indexed;
        }
        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) { return !(lhs == rhs); }

      private:
        std::span<const Candidate> _candidates{};
        std::span<const uint32_t>  _order{};
        size_t                     _position = 0;
        bool                       _indexed  = false;
    };

    CandidateOrderView() = default;

    explicit CandidateOrderView(std::span<const value_type> candidates)
        : _candidates(candidates)
    {}

    CandidateOrderView(std::span<const value_type> candidates, std::span<const uint32_t> order)
        : _candidates(candidates), _order(order), _indexed(true)
    {}

    [[nodiscard]] size_t size() const { return _indexed ? _order.size() : _candidates.size(); }
    [[nodiscard]] bool   empty() const { return size() == 0; }
    [[nodiscard]] const value_type* data() const { return _indexed ? nullptr : _candidates.data(); }

    [[nodiscard]] const value_type& operator[](size_t index) const
    {
        return _candidates[_indexed ? _order[index] : index];
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{_candidates, _order, 0, _indexed}; }
    [[nodiscard]] const_iterator end() const { return const_iterator{_candidates, _order, size(), _indexed}; }

    [[nodiscard]] CandidateOrderView subview(size_t offset, size_t count) const
    {
        if (!_indexed) {
            return CandidateOrderView{_candidates.subspan(offset, count)};
        }
        return CandidateOrderView{_candidates, _order.subspan(offset, count)};
    }

  private:
    std::span<const value_type> _candidates{};
    std::span<const uint32_t>   _order{};
    bool                        _indexed = false;
};

/// Mesh draw candidates ordered by the View.
using DrawCandidateView = CandidateOrderView<RenderDrawItem>;

/// World sprite candidates ordered by the View.
using WorldSpriteView = CandidateOrderView<WorldSpriteCandidate>;

/// Backend-neutral draw grouping metadata.
///
/// This is intentionally not an indirect command mirror. The graph/draw
/// consumer still owns command encoding, while the packet describes the
/// candidate range and the bindings that make the range homogeneous.
struct DrawPacket
{
    DrawCandidateView candidates{};
    Mesh*             mesh            = nullptr;
    Material*         material        = nullptr;
    uint32_t          materialIndex   = 0;
    uint32_t          firstInstance   = 0;
    uint32_t          instanceCount   = 0;
    float             sortKey         = 0.0f;
    bool              bSkinned        = false;

    [[nodiscard]] bool isValid() const
    {
        return mesh != nullptr && !candidates.empty() && instanceCount > 0;
    }
};

/// Group an already deterministic candidate range into contiguous packets.
///
/// The extractor currently sorts opaque candidates by material and mesh. This
/// helper preserves that order and only groups adjacent candidates with the
/// same binding identity; it never reorders or owns the candidate snapshot.
[[nodiscard]] inline std::vector<DrawPacket> buildDrawPackets(DrawCandidateView candidates,
                                                               bool               bSkinned)
{
    std::vector<DrawPacket> packets;
    if (candidates.empty()) {
        return packets;
    }

    size_t groupBegin = 0;
    while (groupBegin < candidates.size()) {
        const auto& first = candidates[groupBegin];
        size_t      groupEnd = groupBegin + 1;
        while (groupEnd < candidates.size()) {
            const auto& current = candidates[groupEnd];
            if (current.mesh != first.mesh ||
                current.material != first.material ||
                current.materialIndex != first.materialIndex) {
                break;
            }
            ++groupEnd;
        }

        const auto group = candidates.subview(groupBegin, groupEnd - groupBegin);
        packets.push_back(DrawPacket{
            .candidates    = group,
            .mesh          = first.mesh,
            .material      = first.material,
            .materialIndex = first.materialIndex,
            .firstInstance = static_cast<uint32_t>(groupBegin),
            .instanceCount = static_cast<uint32_t>(groupEnd - groupBegin),
            .sortKey       = first.sortKey,
            .bSkinned      = bSkinned,
        });
        groupBegin = groupEnd;
    }

    return packets;
}

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

/// View-owned ordering over immutable scene candidates. The source vector is
/// borrowed from SceneSnapshot; only the camera-dependent order is owned
/// by the view.
template <typename Candidate>
struct ViewCandidateBucket
{
    const std::vector<Candidate>* source = nullptr;
    std::vector<uint32_t>          order;

    void clear()
    {
        source = nullptr;
        order.clear();
    }

    [[nodiscard]] CandidateOrderView<Candidate> view() const
    {
        if (!source) {
            return {};
        }
        return CandidateOrderView<Candidate>{std::span<const Candidate>(*source), std::span<const uint32_t>(order)};
    }

    [[nodiscard]] size_t size() const { return source ? order.size() : 0; }
    [[nodiscard]] bool   empty() const { return size() == 0; }
    [[nodiscard]] const Candidate& operator[](size_t index) const { return view()[index]; }
    [[nodiscard]] auto begin() const { return view().begin(); }
    [[nodiscard]] auto end() const { return view().end(); }
    [[nodiscard]] operator CandidateOrderView<Candidate>() const { return view(); }
};

/// Mesh draw candidates ordered by the View.
using ViewDrawBucket = ViewCandidateBucket<RenderDrawItem>;

/// World sprite candidates ordered by the View.
using ViewSpriteBucket = ViewCandidateBucket<WorldSpriteCandidate>;

struct ViewShadingDrawBuckets
{
    ViewDrawBucket pbrDrawItems;
    ViewDrawBucket phongDrawItems;
    ViewDrawBucket unlitDrawItems;
    ViewDrawBucket simpleDrawItems;
    ViewDrawBucket fallbackDrawItems;

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
        return pbrDrawItems.size() + phongDrawItems.size() + unlitDrawItems.size() +
               simpleDrawItems.size() + fallbackDrawItems.size();
    }
};

struct ViewMeshClassDrawBuckets
{
    ViewShadingDrawBuckets staticMeshes;
    ViewShadingDrawBuckets skinnedMeshes;

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

/// Scene-owned directional light data. It deliberately contains no camera or
/// shadow projection state so the snapshot can be shared by multiple views.
struct SceneDirectionalLightData
{
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 color     = glm::vec3(1.0f);
    float     intensity = 1.0f;
};

/// Scene-owned point light data. Shadow matrices are generated for each view
/// when the pipeline-facing RenderFrameData is prepared.
struct ScenePointLightData
{
    glm::vec3 position = glm::vec3(0.0f);
    float     type = 0.0f;

    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;

    glm::vec3 color = glm::vec3(1.0f);
    float     intensity = 1.0f;

    glm::vec3 spotDir = glm::vec3(0.0f, 0.0f, -1.0f);
    float     innerCutOff = 0.0f;
    float     outerCutOff = 0.0f;

    float nearPlane = 0.1f;
    float farPlane  = 100.0f;
};

/// Scene-level render data that can be shared by multiple camera views in the
/// same frame. Camera and shadow state belongs to RenderFrameData below.
struct SceneSnapshot
{
    bool                                                     bHasDirectionalLight = false;
    SceneDirectionalLightData                                directionalLightSource;
    uint32_t                                                 pointLightSourceCount = 0;
    std::array<ScenePointLightData, MAX_POINT_LIGHTS>        pointLightSources;

    RenderMeshClassDrawBuckets drawBuckets;
    std::vector<RenderSkinningPalette> skinningPalettes;
    /// Authored scene sprites (Sprite2DComponent), extracted once for this
    /// Scene+revision and shared by every View.
    std::vector<WorldSpriteCandidate> worldSprites;

    void clearScene()
    {
        bHasDirectionalLight = false;
        directionalLightSource = {};
        pointLightSourceCount  = 0;
        pointLightSources      = {};
        drawBuckets.clear();
        skinningPalettes.clear();
        worldSprites.clear();
    }
};

/// All data a render pipeline needs for one camera view in one frame.
/// The immutable Scene snapshot is shared by all views of the same scene;
/// camera-dependent draw ordering remains in the per-view buckets below.
struct RenderFrameData
{
    std::shared_ptr<const SceneSnapshot>                        sceneSnapshot;
    ViewMeshClassDrawBuckets                                    drawBuckets;
    /// This View's authored sprites: an order over the shared candidates. The
    /// sprite pass reads it, so recording never walks the Scene's ECS again.
    ViewSpriteBucket                                            worldSprites;
    FrameContext::DirectionalLightData                          directionalLight;
    uint32_t                                                   numPointLights = 0;
    std::array<FrameContext::PointLightData, MAX_POINT_LIGHTS> pointLights;

    glm::mat4    view           = glm::mat4(1.0f);
    glm::mat4    projection     = glm::mat4(1.0f);
    glm::mat4    viewProjection = glm::mat4(1.0f);
    glm::vec3    cameraPos      = glm::vec3(0.0f);
    Extent2D     viewExtent = {};
    entt::entity viewOwner      = entt::null;
    /// Features this view draws (see RenderFeatures.h); the extraction bucket
    /// binding filters against it.
    FRenderFeatureMask viewFeatures = toMask(ERenderFeature::Game);

    /// GPU bindings keyed on this View's Scene, resolved before graph build.
    /// Passes read them from here so recording never asks an owner which Scene
    /// is current (see RenderViewSceneResources.h).
    RenderViewSceneResources sceneResources{};

    // Frame-level constants (tick, delta, clock) are NOT here: they are the
    // frame packet's facts, carried once per frame (`FramePacket`) and surfaced
    // to stages through `RenderStageContext` -- one answer, not a per-view copy.

    // ═══════════════════════════════════════════════════════════════
    // Helpers
    // ═══════════════════════════════════════════════════════════════
    void clear()
    {
        sceneSnapshot.reset();
        drawBuckets.clear();
        worldSprites.clear();
        directionalLight = {};
        numPointLights = 0;
        pointLights = {};
        sceneResources.clear();
    }

    [[nodiscard]] size_t totalDrawCount() const
    {
        return drawBuckets.totalDrawCount();
    }
};

} // namespace ya
