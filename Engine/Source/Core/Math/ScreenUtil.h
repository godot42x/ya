#pragma once
#include <glm/glm.hpp>

namespace ya {
    // Utility to convert world position to screen position
    // view: camera view matrix
    // projection: camera projection matrix
    // viewport: (x, y, width, height)
    // worldPos: world space position
    // Returns: screen space position (origin at top-left, y down)
    inline glm::vec2 worldToScreen(const glm::vec3& worldPos, const glm::mat4& view, const glm::mat4& projection, const glm::vec4& viewport)
    {
        glm::vec4 clipSpace = projection * view * glm::vec4(worldPos, 1.0f);
        if (clipSpace.w == 0.0f) return glm::vec2(-1.0f);
        glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
        // NDC [-1,1] to window [x,y,width,height]
        float x = viewport.x + (ndc.x + 1.0f) * 0.5f * viewport.z;
        float y = viewport.y + (1.0f - ndc.y) * 0.5f * viewport.w; // y down
        return glm::vec2(x, y);
    }
}
