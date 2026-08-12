#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include "LineRender.h"
#include "QuadRender.h"

#include <array>
#include <vector>

namespace ya
{

struct IRender;
struct Font;

/// Diagnostics state adjusted live from the runtime tools panel. These are
/// draw-time parameters only; they are not part of a recording session.
struct FRender2dDebugState
{
    ECullMode::T screenCullMode   = ECullMode::None;
    ECullMode::T worldCullMode    = ECullMode::None;
    bool         bReverseViewport = true;
    int          TextLayoutMode   = 0;
};

/// State of one Render2D recording session, valid between begin()/end().
/// After end() the command buffer is cleared; any draw call outside a session
/// is asserted instead of silently no-op'ing.
struct FRender2dSession
{
    ICommandBuffer*  curCmdBuf   = nullptr;
    uint32_t         windowWidth  = 800;
    uint32_t         windowHeight = 600;
    Render2DPassSlot passSlot     = 0;
    // World-space draw transform (world sprites / debug lines). Screen-space
    // UI never reads these; it uses its own orthographic projection.
    glm::mat4        view          = glm::mat4(1.0f);
    glm::mat4        viewProjection = glm::mat4(1.0f);

    // Active screen-space clip rects (top-left origin, Y down). The top entry
    // is applied as the scissor on the next screen-batch flush.
    std::vector<Rect2D> clipStack;
};

struct FRender2dContext
{
    ICommandBuffer*  cmdBuf       = nullptr;
    uint32_t         windowWidth  = 800;
    uint32_t         windowHeight = 600;
    Render2DPassSlot passSlot     = 0;
    glm::mat4        view          = glm::mat4(1.0f);
    glm::mat4        viewProjection = glm::mat4(1.0f);
};

struct YA_GUI_API Render2D
{
    static FQuadRender*  quadData;
    static FLineRender*  lineData;
    static FRender2dDebugState debug;
    static FRender2dSession    session;

    Render2D()          = default;
    virtual ~Render2D() = default;

    static void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    static void destroy();

    static void onUpdate(float dt);
    static void onRender();

    static void begin(const FRender2dContext& ctx);
    static void end();

    /// Acquire a unique pass slot for this Render2D instance. Call once at
    /// setup; the returned index is caller-owned and maps the caller's own
    /// pass vocabulary onto Render2D's per-pass resources.
    [[nodiscard]] static Render2DPassSlot acquirePassSlot();

    /// Push a clip rect (intersected with the current clip). Changes are applied
    /// as a command-level scissor on the next screen batch flush.
    static void pushClipRect(const Rect2D& rect);
    static void popClipRect();

    /// Pure clip intersection used by the clip stack: `rect` clipped to the
    /// current `parentClip` (empty extent when disjoint). Extracted so the
    /// nested-clip semantics are unit-testable without a render session.
    [[nodiscard]] static Rect2D intersectClipRect(const Rect2D& rect, const Rect2D& parentClip);

    /// Lazily create the screen-space pipeline variant required by one pass
    /// slot. A depth-less target (depthFormat == Undefined) uses the depth-less
    /// UI variant; a depth-attached target uses the depth-aware screen variant.
    /// Must NOT be called while recording a command buffer.
    static void preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat);

    static void makeSprite(const glm::vec3& position,
                           const glm::vec2& size,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(position, size, texture, tint, uvScale);
    }

    static void makeSprite(const glm::mat4& transform,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawTexture(transform, texture, tint, uvScale);
    }

    static void makeWorldSprite(const glm::vec3& worldCenter,
                                const glm::vec3& worldDirection,
                                const glm::vec2& worldSize,
                                ya::Ptr<Texture> texture = nullptr,
                                const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                                const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        quadData->drawWorldTexture(worldCenter, worldDirection, worldSize, texture, tint, uvScale);
    }

    static void makeWorldLine(const glm::vec3& from,
                              const glm::vec3& to,
                              const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        lineData->addLine(from, to, color);
    }

    static void makeWireBox(const glm::mat4& model,
                            const glm::vec3& halfExtent,
                            const glm::vec4& color = {0.2f, 0.9f, 0.3f, 1.0f})
    {
        lineData->addWireBox(model, halfExtent, color);
    }

    static void makeWireSphere(const glm::vec3& center,
                               float            radius,
                               const glm::vec4& color = {0.3f, 0.6f, 1.0f, 1.0f})
    {
        lineData->addWireSphere(center, radius, color);
    }

    static void makeText(const std::string& text,
                         const glm::vec3&   position,
                         const glm::vec4&   color,
                         Font*              font,
                         const glm::vec2&   scale = glm::vec2(1.0f))
    {
        quadData->drawText(text, position, color, font, scale);
    }
};

} // namespace ya
