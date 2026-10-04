#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/ShadowSettings.h"

#include <unordered_map>

namespace ya
{

struct Scene;
class TerrainProcessor;
struct RenderRuntime;
struct SkeletonAnimatorComponent;
struct Sprite2DComponent;
struct TilemapComponent;

struct RenderFrameExtractor
{
    struct SceneExtractInput
    {
        Scene*             scene            = nullptr;
        TerrainProcessor* terrainProcessor = nullptr;
    };

    struct DrawItemExtractionContext
    {
        entt::registry*      registry      = nullptr;
        SceneSnapshot*        sceneSnapshot = nullptr;
        Scene*               scene         = nullptr;
        TerrainProcessor* terrainProcessor = nullptr;
        std::unordered_map<const SkeletonAnimatorComponent*, int32_t> skinningPaletteCache;
    };

    struct ViewPrepareInput
    {
        glm::mat4      view           = glm::mat4(1.0f);
        glm::mat4      projection     = glm::mat4(1.0f);
        glm::mat4      viewProjection = glm::mat4(1.0f);
        glm::vec3      cameraPos      = glm::vec3(0.0f);
        Extent2D       viewExtent = {};
        entt::entity   viewOwner  = entt::null;
        /// Features this view draws; the bucket binding filters the immutable
        /// snapshot against it (see RenderFeatures.h).
        FRenderFeatureMask viewFeatures = toMask(ERenderFeature::Game);
        const ShadowSettings* shadowSettings = nullptr;
    };

    /// Extract only Scene/ECS-owned data. The result is independent of camera
    /// matrices and can be shared by multiple viewport views in one frame.
    static void extractSceneSnapshot(const SceneExtractInput& input, SceneSnapshot& outSnapshot);

    /// Build the pipeline-facing view packet from one immutable Scene snapshot.
    /// Camera-dependent shadow preparation and draw sorting happen here.
    static void prepareView(const ViewPrepareInput& input,
                            std::shared_ptr<const SceneSnapshot>      sceneSnapshot,
                            RenderFrameData& outFrame);

    /// One entity's candidate: the world matrix and the component are everything
    /// the quad needs (see WorldSpriteCandidate). Split out of the ECS walk so
    /// the transform/UV math is checkable on its own, and so the walk stays a
    /// gate plus this call.
    [[nodiscard]] static WorldSpriteCandidate buildSpriteCandidate(const glm::mat4&         world,
                                                                   const Sprite2DComponent& sprite,
                                                                   uint32_t                 entityId);

    /// Snap sprite and tile centers onto the texel grid (`1 / pixelsPerUnit`).
    /// Candidates are shared by every View, so this is not the per-view device
    /// pixel (`1 / (pixelsPerUnit * zoom)`): an integer zoom divides that texel,
    /// and the camera eye is snapped per view, so a texel-aligned sprite still
    /// lands on a device-pixel boundary. A non-positive step leaves them alone.
    static void snapSpriteCandidatesToTexelGrid(std::vector<WorldSpriteCandidate>& sprites, float texelStep);

  private:
    static void extractCamera(const ViewPrepareInput& input, RenderFrameData& out);
    static void extractSceneLights(entt::registry& reg, SceneSnapshot& out);
    static void extractSprites(Scene* scene, entt::registry& reg, SceneSnapshot& out);
    static void extractTilemaps(Scene* scene, entt::registry& reg, SceneSnapshot& out);
    static void prepareViewLights(const ViewPrepareInput& input, RenderFrameData& out);
    static int32_t registerSkinningPalette(DrawItemExtractionContext& ctx, entt::entity entity, Mesh* mesh);
    static void extractDrawItems(DrawItemExtractionContext& ctx);
    /// Order every View-owned bucket. Mesh buckets are camera-distance ordered.
    /// Sprites use the painter key, which does not depend on the camera.
    static void sortViewBuckets(const glm::vec3& cameraPos, RenderFrameData& out);
};

} // namespace ya
