#pragma once

#include "glm/glm.hpp"

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/Texture.h"
#include "RHI/RenderDefines.h"

#include "Render2D/LineRender.h"
#include "Render2D/QuadRender.h"

#include <array>
#include <vector>

namespace ya
{

struct IRender;
struct Font;

/// Which Render2D backend currently holds unflushed geometry. The session
/// keeps at most one of these live so GPU submit order matches emit order
/// across screen quads, world quads, and debug lines.
enum class ERender2dBatchKind : uint8_t
{
    None = 0,
    ScreenQuad,
    WorldQuad,
    Line,
};

/// Diagnostics state adjusted live from the runtime tools panel. These are
/// draw-time parameters only; they are not part of a recording session.
struct FRender2dDebugState
{
    ECullMode::T screenCullMode   = ECullMode::None;
    ECullMode::T worldCullMode    = ECullMode::None;
    bool         bReverseViewport = true;
    int          TextLayoutMode   = 0;
    bool         bLogSessionLifecycle = false;
    bool         bLogClipStack        = false;
    bool         bLogFlushBatches     = false;
    uint32_t     maxClipLogsPerFrame  = 16;
    uint32_t     maxFlushLogsPerFrame = 16;
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
    ERender2dBatchKind  pendingKind       = ERender2dBatchKind::None;
    uint32_t            debugClipLogCount = 0;
    uint32_t            debugScreenFlushCount = 0;
    uint32_t            debugWorldFlushCount = 0;
    uint32_t            screenFlushCount      = 0;
    uint32_t            worldFlushCount       = 0;
    uint32_t            screenVertexCount     = 0;
    uint32_t            screenIndexCount      = 0;
};

/// GPU counters from the most recently ended Render2D session. Independent of
/// `bLogFlushBatches` (that flag only limits log lines).
struct FRender2dFrameStats
{
    uint32_t screenFlushCount  = 0;
    uint32_t worldFlushCount   = 0;
    uint32_t screenVertexCount = 0;
    uint32_t screenIndexCount  = 0;
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

struct YA_RENDER_2D_API Render2D
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

    /// Acquire a unique pass slot. Call once per window (or per window+kind
    /// that records into the same in-flight command buffer). Release when the
    /// window is destroyed after that surface's GPU work has finished.
    [[nodiscard]] static Render2DPassSlot acquirePassSlot();
    static void                           releasePassSlot(Render2DPassSlot slot);

    /// Push a clip rect (intersected with the current clip). Changes are applied
    /// as a command-level scissor on the next screen batch flush. Any pending
    /// backend is flushed first so already-recorded geometry keeps the current
    /// scissor / draw-order slot.
    static void pushClipRect(const Rect2D& rect);
    static void popClipRect();

    /// Bind the session to `kind`. Flushes the previous backend if the kind
    /// changed so GPU submit order matches emit order across pipelines.
    static void beginBatch(ERender2dBatchKind kind);
    /// Flush the currently pending backend without changing kind.
    static void flushPending();

    /// Pure clip intersection used by the clip stack: `rect` clipped to the
    /// current `parentClip` (empty extent when disjoint). Extracted so the
    /// nested-clip semantics are unit-testable without a render session.
    [[nodiscard]] static Rect2D intersectClipRect(const Rect2D& rect, const Rect2D& parentClip);

    /// Lazily create the screen/line pipeline variants required by one pass
    /// slot. A depth-less target (depthFormat == Undefined) uses the depth-less
    /// UI variant; a depth-attached target uses the depth-aware screen variant
    /// and refreshes the shared world sprite pipeline. Must NOT be called
    /// while recording a command buffer.
    static void preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat);
    [[nodiscard]] static bool isInitialized();

    // Accessors to the singleton primitives. These are exported functions
    // (defined in Render2D.cpp) rather than direct references to the static
    // quadData/lineData members: inline helpers in this header that touch the
    // statics get expanded inside *every* consuming DLL, where the dllexport
    // macro propagation makes the data symbol look "defined here" and linking
    // fails (LNK2001). Routing through a function keeps the data inside the
    // owning DLL.
    [[nodiscard]] static FQuadRender* quadRender();
    [[nodiscard]] static FLineRender* lineRender();

    // Accessors to the diagnostic/session singletons. Same rationale as
    // quadRender()/lineRender(): `debug`/`session` are static data members that
    // must not be referenced directly across the DLL boundary (a dllexport data
    // symbol cannot be imported from another DLL).
    [[nodiscard]] static FRender2dDebugState& debugState();
    [[nodiscard]] static FRender2dSession&    sessionState();
    [[nodiscard]] static const FRender2dFrameStats& lastFrameStats();

    static void makeSprite(const glm::vec3& position,
                           const glm::vec2& size,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f},
                           const glm::vec2& uvOffset = {0.0f, 0.0f},
                           bool             bOpaqueSample = false)
    {
        beginBatch(ERender2dBatchKind::ScreenQuad);
        quadRender()->drawTexture(position, size, texture, tint, uvScale, uvOffset, bOpaqueSample);
    }

    static void makeSprite(const glm::mat4& transform,
                           ya::Ptr<Texture> texture = nullptr,
                           const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                           const glm::vec2& uvScale = {1.0f, 1.0f},
                           const glm::vec2& uvOffset = {0.0f, 0.0f},
                           bool             bOpaqueSample = false)
    {
        beginBatch(ERender2dBatchKind::ScreenQuad);
        quadRender()->drawTexture(transform, texture, tint, uvScale, uvOffset, bOpaqueSample);
    }

    static void makeWorldSprite(const glm::vec3& worldCenter,
                                const glm::vec3& worldDirection,
                                const glm::vec2& worldSize,
                                ya::Ptr<Texture> texture = nullptr,
                                const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                                const glm::vec2& uvScale = {1.0f, 1.0f})
    {
        beginBatch(ERender2dBatchKind::WorldQuad);
        quadRender()->drawWorldTexture(worldCenter, worldDirection, worldSize, texture, tint, uvScale);
    }

    static void makeWorldLine(const glm::vec3& from,
                              const glm::vec3& to,
                              const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        beginBatch(ERender2dBatchKind::Line);
        lineRender()->addLine(from, to, color);
    }

    static void makeWireBox(const glm::mat4& model,
                            const glm::vec3& halfExtent,
                            const glm::vec4& color = {0.2f, 0.9f, 0.3f, 1.0f})
    {
        beginBatch(ERender2dBatchKind::Line);
        lineRender()->addWireBox(model, halfExtent, color);
    }

    static void makeWireSphere(const glm::vec3& center,
                               float            radius,
                               const glm::vec4& color = {0.3f, 0.6f, 1.0f, 1.0f})
    {
        beginBatch(ERender2dBatchKind::Line);
        lineRender()->addWireSphere(center, radius, color);
    }

    static void makeText(const std::string& text,
                         const glm::vec3&   position,
                         const glm::vec4&   color,
                         Font*              font,
                         const glm::vec2&   scale = glm::vec2(1.0f))
    {
        beginBatch(ERender2dBatchKind::ScreenQuad);
        quadRender()->drawText(text, position, color, font, scale);
    }

    /// Draw a filled rounded rectangle. `cornerRadius` is in target px; the
    /// shader derives the SDF round-rect alpha from the quad size. No texture
    /// is sampled (a white sprite is used as the fill).
    static void drawRoundedRect(const glm::vec3& position,
                                const glm::vec2& size,
                                const glm::vec4& tint,
                                float            cornerRadius)
    {
        beginBatch(ERender2dBatchKind::ScreenQuad);
        quadRender()->drawRoundedRect(position, size, tint, cornerRadius);
    }

    /// Screen quad with a different color on each corner. `colors` is Y-down
    /// ImGui order: top-left, top-right, bottom-right, bottom-left. GPU
    /// interpolates vertex color; no extra texture or offscreen pass.
    static void makeRectFilledMultiColor(const glm::vec3&               position,
                                         const glm::vec2&               size,
                                         const std::array<glm::vec4, 4>& colors,
                                         ya::Ptr<Texture>               texture = nullptr)
    {
        beginBatch(ERender2dBatchKind::ScreenQuad);
        quadRender()->drawRectFilledMultiColor(position, size, colors, texture);
    }
};

} // namespace ya
