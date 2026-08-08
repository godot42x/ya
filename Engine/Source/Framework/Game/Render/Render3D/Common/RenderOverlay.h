#pragma once

#include "Foundation/Core/Base.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct Texture;
struct FrameContext;
struct ICommandBuffer;

struct RenderOverlaySprite2D
{
    glm::vec2 viewportPos = glm::vec2(0.0f);
    glm::vec2 size        = glm::vec2(50.0f);
    Texture*  texture     = nullptr;
    glm::vec4 tint        = glm::vec4(1.0f);
};

struct RenderOverlaySprite3D
{
    glm::vec3 worldCenter    = glm::vec3(0.0f);
    glm::vec3 worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec2 worldSize      = glm::vec2(1.0f);
    Texture*  texture        = nullptr;
    glm::vec4 tint           = glm::vec4(1.0f);
};

struct RenderOverlayText2D
{
    std::string text{};
    glm::vec2   viewportPos = glm::vec2(0.0f);
    glm::vec4   color       = glm::vec4(1.0f);
    uint32_t    fontSize    = 16;
    float       depth       = 0.0f;
};

struct RenderViewportOverlaySnapshot
{
    std::vector<RenderOverlaySprite2D> screenSprites{};
    std::vector<RenderOverlaySprite3D> worldSprites{};
    std::vector<RenderOverlayText2D>   screenTexts{};

    [[nodiscard]] bool empty() const
    {
        return screenSprites.empty() && worldSprites.empty() && screenTexts.empty();
    }
};

/// Record the world/screen overlay pass (sprites, debug lines, texts) inside
/// one Render2D recording window. Engine-side: consumed by the 3D frame graph
/// viewport composition, kept out of the UI framework closure.
void recordRenderViewportOverlayPass(const FrameContext& frameCtx,
                                     const std::shared_ptr<const RenderViewportOverlaySnapshot>& overlaySnapshot,
                                     ICommandBuffer* cmdBuf);

} // namespace ya
