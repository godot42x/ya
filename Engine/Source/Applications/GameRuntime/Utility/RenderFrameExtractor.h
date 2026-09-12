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
        WorldFrameSnapshot*  worldSnapshot = nullptr;
        entt::entity         viewOwner     = entt::null;

        TerrainProcessor* terrainProcessor = nullptr;
        std::unordered_map<const SkeletonAnimatorComponent*, int32_t> skinningPaletteCache;
    };

    struct ExtractInput
    {
        Scene*         scene      = nullptr;
        glm::mat4      view           = glm::mat4(1.0f);
        glm::mat4      projection     = glm::mat4(1.0f);
        glm::mat4      viewProjection = glm::mat4(1.0f);
        glm::vec3      cameraPos      = glm::vec3(0.0f);
        Extent2D       viewportExtent = {};
        entt::entity   viewOwner  = entt::null;
        uint64_t       frameIndex = 0;
        float          deltaTime  = 0.0f;
        const ShadowSettings* shadowSettings = nullptr;
        TerrainProcessor*     terrainProcessor = nullptr;
    };

    /// Extract a complete render frame snapshot from the scene.
    static void extract(const ExtractInput& input, RenderFrameData& outFrame);

    /// Extract only Scene/ECS-owned data. The result is independent of camera
    /// matrices and can be shared by multiple viewport views in one frame.
    static void extractSceneSnapshot(const SceneExtractInput& input, WorldFrameSnapshot& outSnapshot);

  private:
    static void extractCamera(const ExtractInput& input, RenderFrameData& out);
    static void extractSceneLights(entt::registry& reg, WorldFrameSnapshot& out);
    static void prepareViewLights(const ExtractInput& input, WorldFrameSnapshot& out);
    static int32_t registerSkinningPalette(DrawItemExtractionContext& ctx, entt::entity entity, Mesh* mesh);
    static void extractDrawItems(DrawItemExtractionContext& ctx);
    static void sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out);
};

} // namespace ya
