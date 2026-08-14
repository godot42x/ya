#pragma once

#include "Render3D/RenderFrameData.h"
#include "Render3D/Common/ShadowSettings.h"

#include <unordered_map>

namespace ya
{

struct Scene;
struct RenderRuntime;
struct SkeletonAnimatorComponent;

struct RenderFrameExtractor
{
    struct DrawItemExtractionContext
    {
        entt::registry* registry  = nullptr;
        RenderFrameData* frameData = nullptr;
        entt::entity    viewOwner = entt::null;

        std::unordered_map<const SkeletonAnimatorComponent*, int32_t> skinningPaletteCache;
    };

    struct ExtractInput
    {
        Scene*         scene      = nullptr;
        glm::mat4      view       = glm::mat4(1.0f);
        glm::mat4      projection = glm::mat4(1.0f);
        glm::vec3      cameraPos  = glm::vec3(0.0f);
        Extent2D       viewportExtent = {};
        entt::entity   viewOwner  = entt::null;
        uint64_t       frameIndex = 0;
        float          deltaTime  = 0.0f;
        const ShadowSettings* shadowSettings = nullptr;
    };

    /// Extract a complete render frame snapshot from the scene.
    static void extract(const ExtractInput& input, RenderFrameData& outFrame);

  private:
    static void extractCamera(const ExtractInput& input, RenderFrameData& out);
    static void extractLights(const ExtractInput& input, entt::registry& reg, RenderFrameData& out);
    static int32_t registerSkinningPalette(DrawItemExtractionContext& ctx, entt::entity entity, Mesh* mesh);
    static void extractDrawItems(DrawItemExtractionContext& ctx);
    static void sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out);
};

} // namespace ya
