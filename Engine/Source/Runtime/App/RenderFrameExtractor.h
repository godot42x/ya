#pragma once

#include "Render/RenderFrameData.h"

namespace ya
{

struct Scene;
struct RenderRuntime;

struct RenderFrameExtractor
{
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
    };

    /// Extract a complete render frame snapshot from the scene.
    static void extract(const ExtractInput& input, RenderFrameData& outFrame);

  private:
    static void extractCamera(const ExtractInput& input, RenderFrameData& out);
    static void extractLights(entt::registry& reg, RenderFrameData& out);
    static void extractSkybox(Scene* scene, RenderFrameData& out);
    static void extractDrawItems(entt::registry& reg, entt::entity viewOwner, RenderFrameData& out);
    static void sortDrawItems(const glm::vec3& cameraPos, RenderFrameData& out);
};

} // namespace ya
