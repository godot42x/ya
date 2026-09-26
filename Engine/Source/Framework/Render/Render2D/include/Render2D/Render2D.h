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
#include "Render2D/Render2DList.h"

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
    bool         bLogSessionLifecycle = false;
    bool         bLogClipStack        = false;
    bool         bLogFlushBatches     = false;
    uint32_t     maxClipLogsPerFrame  = 16;
    uint32_t     maxFlushLogsPerFrame = 16;
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
    /// The device both batchers record through; the record step resolves the
    /// per-frame ring slot from it (`framesInFlight` / frame ordinal).
    static IRender*      device;
    static FRender2dDebugState debug;

    static void init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat);
    static void destroy();

    /// Acquire a unique pass slot. Call once per window (or per window+kind
    /// that records into the same in-flight command buffer). Release when the
    /// window is destroyed after that surface's GPU work has finished.
    [[nodiscard]] static Render2DPassSlot acquirePassSlot();
    static void                           releasePassSlot(Render2DPassSlot slot);

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

    [[nodiscard]] static FRender2dDebugState& debugState();
    [[nodiscard]] static const FQuadRender::FRender2dFrameStats& lastFrameStats();

    /// Turn a recorded 2D draw list into GPU work inside `ctx`: resolves the
    /// flight slot, binds the pass's per-slot/per-frame resources, re-keys the
    /// list's local texture table into the pass binding table, and replays the
    /// command stream through the same flush boundaries the immediate path
    /// used (kind change, clip change, region and texture-table capacity).
    [[nodiscard]] static FQuadRender::FRender2dFrameStats recordRender2DList(const Render2DList& list,
                                                                            const FRender2dContext& ctx);
};

} // namespace ya
