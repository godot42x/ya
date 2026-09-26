#include "Render2D/Render2D.h"

#include "Core/Log.h"
#include "RHI/Core/CommandBuffer.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace ya
{

FRender2dDebugState Render2D::debug;
FRender2dSession    Render2D::session;
FQuadRender*        Render2D::quadData = nullptr;
FLineRender*        Render2D::lineData = nullptr;
IRender*            Render2D::device   = nullptr;

namespace
{
FQuadRender::FQuadRender::FRender2dFrameStats gLastFrameStats{};
uint32_t sRecordDebugScreenFlushCount = 0;
uint32_t sRecordDebugWorldFlushCount  = 0;

struct PassSlotPool
{
    Render2DPassSlot              next = 0;
    std::vector<Render2DPassSlot> free;
};

PassSlotPool& passSlotPool()
{
    static PassSlotPool pool;
    return pool;
}
}

FQuadRender* Render2D::quadRender() { return quadData; }
FLineRender* Render2D::lineRender() { return lineData; }

FRender2dDebugState& Render2D::debugState() { return debug; }
FRender2dSession&    Render2D::sessionState() { return session; }
const FQuadRender::FRender2dFrameStats& Render2D::lastFrameStats() { return gLastFrameStats; }

void Render2D::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    YA_CORE_ASSERT(!isInitialized(), "Render2D::init called while already initialized");
    device   = render;
    quadData = new FQuadRender();
    quadData->init(render, colorFormat, depthFormat);

    lineData = new FLineRender();
    lineData->init(render, colorFormat, depthFormat);
}

void Render2D::destroy()
{
    if (lineData) {
        lineData->destroy();
        delete lineData;
        lineData = nullptr;
    }

    if (quadData) {
        quadData->destroy();
        delete quadData;
        quadData = nullptr;
    }
    device = nullptr;
}

bool Render2D::isInitialized()
{
    return quadData != nullptr;
}

void Render2D::onUpdate(float dt)
{
    (void)dt;
}

void Render2D::onRender()
{
}

void Render2D::begin(const FRender2dContext& ctx)
{
    // A stale session means an earlier pass forgot to call end(); fail loudly
    // instead of leaking clip state and command buffer into the next pass.
    YA_CORE_ASSERT(session.curCmdBuf == nullptr,
                   "Render2D::begin called while a recording session is still active (missing end()?)");
    session.curCmdBuf    = ctx.cmdBuf;
    session.view          = ctx.view;
    session.viewProjection = ctx.viewProjection;
    session.windowHeight  = ctx.windowHeight;
    session.windowWidth   = ctx.windowWidth;
    session.passSlot      = ctx.passSlot;
    session.clipStack.clear();
    gLastFrameStats = {};
    session.pendingKind           = ERender2dBatchKind::None;
    session.debugClipLogCount     = 0;
    session.debugScreenFlushCount = 0;
    session.debugWorldFlushCount  = 0;
    session.screenFlushCount      = 0;
    session.worldFlushCount       = 0;
    session.screenVertexCount     = 0;
    session.screenIndexCount      = 0;
    if (debug.bLogSessionLifecycle) {
        YA_CORE_INFO("Render2D begin: passSlot={} extent={}x{} cmdBuf={} reverseViewport={}",
                     static_cast<size_t>(ctx.passSlot),
                     ctx.windowWidth,
                     ctx.windowHeight,
                     static_cast<const void*>(ctx.cmdBuf),
                     debug.bReverseViewport);
    }
    Extent2D extent{.width = session.windowWidth, .height = session.windowHeight};
    // One derivation for both batchers: which slot of a pass's per-frame ring
    // this recording may use. The bound is the device's frames in flight -- the
    // frame ordinal modulo that depth -- so with one frame in flight every
    // recording uses slot 0, and raising the depth (the CPU/GPU overlap
    // decision, temporal_semantics M4) is what makes the slots rotate.
    const uint32_t framesInFlight = device ? std::max(1u, device->framesInFlight()) : 1u;
    const uint32_t flightSlot     = static_cast<uint32_t>(device ? device->recordedFrameIndex() % framesInFlight : 0u);
    quadData->begin(ctx.passSlot, extent, flightSlot);
    lineData->begin(ctx.passSlot, flightSlot);
}

void Render2D::end()
{
    flushPending();
    session.pendingKind = ERender2dBatchKind::None;
    YA_CORE_ASSERT(quadData->vertexCount == 0 && quadData->worldVertexCount == 0,
                   "Render2D end() left unflushed quads (screen={} world={})",
                   quadData->vertexCount,
                   quadData->worldVertexCount);
    YA_CORE_ASSERT(lineData->vertexCount == 0,
                   "Render2D end() left unflushed lines (count={})",
                   lineData->vertexCount);

    if (debug.bLogSessionLifecycle) {
        YA_CORE_INFO("Render2D end: passSlot={} screenFlushes={} worldFlushes={} remainingClipDepth={}",
                     static_cast<size_t>(session.passSlot),
                     session.debugScreenFlushCount,
                     session.debugWorldFlushCount,
                     session.clipStack.size());
    }

    session.curCmdBuf    = nullptr;
    session.windowWidth  = 0;
    session.windowHeight = 0;
}

Render2DPassSlot Render2D::acquirePassSlot()
{
    auto& pool = passSlotPool();
    if (!pool.free.empty()) {
        const Render2DPassSlot slot = pool.free.back();
        pool.free.pop_back();
        return slot;
    }
    const Render2DPassSlot slot = pool.next++;
    YA_CORE_ASSERT(slot < FQuadRender::kMaxPassSlots,
                   "Render2D pass slot pool exhausted ({} slots)",
                   FQuadRender::kMaxPassSlots);
    return slot;
}

void Render2D::releasePassSlot(Render2DPassSlot slot)
{
    if (slot == kInvalidRender2DPassSlot) {
        return;
    }
    passSlotPool().free.push_back(slot);
}

void Render2D::preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (quadData) {
        quadData->preparePassPipeline(passSlot, colorFormat, depthFormat);
    }
    if (lineData) {
        lineData->preparePassPipeline(passSlot, colorFormat, depthFormat);
    }
}

void Render2D::pushClipRect(const Rect2D& rect)
{
    // Intersect with the current clip so nested clips never exceed their parent.
    Rect2D clipped = rect;
    if (!session.clipStack.empty()) {
        clipped = intersectClipRect(rect, session.clipStack.back());
    }

    const bool bClipChanged = session.clipStack.empty() ||
                              session.clipStack.back().pos != clipped.pos ||
                              session.clipStack.back().extent != clipped.extent;
    if (bClipChanged && session.curCmdBuf) {
        // Flush whichever backend is pending with the CURRENT scissor / draw
        // slot BEFORE switching clip; otherwise already-recorded geometry is
        // either culled by the incoming scissor or submitted after later draws.
        flushPending();
    }
    session.clipStack.push_back(clipped);
    if (debug.bLogClipStack && session.debugClipLogCount < debug.maxClipLogsPerFrame) {
        ++session.debugClipLogCount;
        YA_CORE_INFO("Render2D pushClip: depth={} changed={} requested=({}, {}) + ({}, {}) clipped=({}, {}) + ({}, {})",
                     session.clipStack.size(),
                     bClipChanged,
                     rect.pos.x,
                     rect.pos.y,
                     rect.extent.x,
                     rect.extent.y,
                     clipped.pos.x,
                     clipped.pos.y,
                     clipped.extent.x,
                     clipped.extent.y);
    }
}

Rect2D Render2D::intersectClipRect(const Rect2D& rect, const Rect2D& parentClip)
{
    const glm::vec2 parentMax = parentClip.pos + parentClip.extent;
    const glm::vec2 rectMax   = rect.pos + rect.extent;
    const glm::vec2 clippedPos    = glm::max(rect.pos, parentClip.pos);
    const glm::vec2 clippedExtent = glm::max(glm::vec2(0.0f), glm::min(rectMax, parentMax) - clippedPos);
    return Rect2D{.pos = clippedPos, .extent = clippedExtent};
}

void Render2D::popClipRect()
{
    if (session.clipStack.empty()) {
        return;
    }
    const Rect2D currentClip = session.clipStack.back();
    if (session.curCmdBuf) {
        // Flush the pending backend with the CURRENT (inner) scissor BEFORE
        // popping; otherwise content recorded inside the clip escapes it.
        flushPending();
    }
    session.clipStack.pop_back();
    if (debug.bLogClipStack && session.debugClipLogCount < debug.maxClipLogsPerFrame) {
        ++session.debugClipLogCount;
        YA_CORE_INFO("Render2D popClip: depthAfterPop={} clip=({}, {}) + ({}, {})",
                     session.clipStack.size(),
                     currentClip.pos.x,
                     currentClip.pos.y,
                     currentClip.extent.x,
                     currentClip.extent.y);
    }
}

void Render2D::flushPending()
{
    // Transitional shim: the legacy immediate path expresses its flush state
    // through the session; the value path (`recordRender2DList`) builds the
    // same state from the frame context instead.
    FQuadRender::FRender2dFlushState state{
        .windowWidth  = session.windowWidth,
        .windowHeight = session.windowHeight,
        .bClipped     = !session.clipStack.empty(),
        .clip         = session.clipStack.empty() ? Rect2D{} : session.clipStack.back(),
        .view         = session.view,
        .viewProjection = session.viewProjection,
        .stats        = &gLastFrameStats,
        .debugScreenFlushCount = &session.debugScreenFlushCount,
        .debugWorldFlushCount  = &session.debugWorldFlushCount,
    };
    switch (session.pendingKind) {
    case ERender2dBatchKind::ScreenQuad:
        if (quadData) {
            quadData->flush(session.curCmdBuf, state);
        }
        break;
    case ERender2dBatchKind::WorldQuad:
        if (quadData) {
            quadData->flushWorld(session.curCmdBuf, state);
        }
        break;
    case ERender2dBatchKind::Line:
        if (lineData) {
            lineData->flush(session.curCmdBuf, state);
        }
        break;
    case ERender2dBatchKind::None:
        break;
    }
}

void Render2D::beginBatch(ERender2dBatchKind kind)
{
    YA_CORE_ASSERT(session.curCmdBuf != nullptr,
                   "Render2D draw called outside a begin()/end() recording session");
    if (session.pendingKind == kind) {
        return;
    }
    flushPending();
    session.pendingKind = kind;
}


FQuadRender::FRender2dFrameStats Render2D::recordRender2DList(const Render2DList& list, const FRender2dContext& ctx)
{
    FQuadRender::FRender2dFrameStats stats{};
    if (!ctx.cmdBuf || !quadData || !lineData) {
        return stats;
    }

    // One derivation for both batchers: which slot of a pass's per-frame ring
    // this recording may use (the device's frames in flight -- never a
    // swapchain's image counter).
    const uint32_t framesInFlight = device ? std::max(1u, device->framesInFlight()) : 1u;
    const uint32_t flightSlot     = static_cast<uint32_t>(device ? device->recordedFrameIndex() % framesInFlight : 0u);
    quadData->begin(ctx.passSlot, Extent2D{.width = ctx.windowWidth, .height = ctx.windowHeight}, flightSlot);
    lineData->begin(ctx.passSlot, flightSlot);

    FQuadRender::FRender2dFlushState state{
        .windowWidth  = ctx.windowWidth,
        .windowHeight = ctx.windowHeight,
        .view         = ctx.view,
        .viewProjection = ctx.viewProjection,
        .stats        = &stats,
        .debugScreenFlushCount = &sRecordDebugScreenFlushCount,
        .debugWorldFlushCount  = &sRecordDebugWorldFlushCount,
    };
    sRecordDebugScreenFlushCount = 0;
    sRecordDebugWorldFlushCount  = 0;

    // List-local texture slot -> pass-global binding slot. Rebuilt whenever
    // the global table overflows (16 textures), exactly like the immediate
    // path's flushForTextureOverflow.
    constexpr uint32_t kUnmapped = ~0u;
    std::vector<uint32_t> localToGlobal(list.textures.size(), kUnmapped);

    static const bool bDumpCommands = std::getenv("YA_R2D_DUMP") != nullptr;
    if (bDumpCommands) {
        YA_CORE_INFO("R2D dump record: commands={} screenVerts={} worldVerts={} lineVerts={} textures={}",
                     list.commands.size(), list.screenVerts.size(), list.worldVerts.size(),
                     list.lineVerts.size(), list.textures.size());
        uint32_t dumpQuad = 0;
        for (const Render2DList::Command& c : list.commands) {
            uint32_t localSlot = 0u;
            if (c.kind == ERender2dBatchKind::ScreenQuad && c.vertexCount > 0) {
                localSlot = list.screenVerts[c.firstVertex].textureRef & FQuadRender::kTextureIndexMask;
            }
            else if (c.kind == ERender2dBatchKind::WorldQuad && c.vertexCount > 0) {
                localSlot = list.worldVerts[c.firstVertex].textureRef & FQuadRender::kTextureIndexMask;
            }
            YA_CORE_INFO("R2D cmd kind={} first={} count={} clipped={} clip=({},{},{},{}) localTex={}",
                         static_cast<int>(c.kind), c.firstVertex, c.vertexCount,
                         c.bClipped, c.clip.pos.x, c.clip.pos.y, c.clip.extent.x, c.clip.extent.y,
                         localSlot);
            if (c.kind == ERender2dBatchKind::ScreenQuad && dumpQuad < 1) {
                for (uint32_t v = 0; v < 4; ++v) {
                    const auto& vert = list.screenVerts[c.firstVertex + v];
                    YA_CORE_INFO("R2D v{} pos=({:.2f},{:.2f},{:.2f}) color=({:.3f},{:.3f},{:.3f},{:.3f}) uv=({:.3f},{:.3f}) texRef={:x} corner=({:.1f},{:.1f},{:.1f})",
                                 v,
                                 vert.pos.x, vert.pos.y, vert.pos.z,
                                 vert.color.r, vert.color.g, vert.color.b, vert.color.a,
                                 vert.texCoord.x, vert.texCoord.y,
                                 vert.textureRef,
                                 vert.corner.x, vert.corner.y, vert.corner.z);
                }
                ++dumpQuad;
            }
        }
    }

    ERender2dBatchKind pending = ERender2dBatchKind::None;
    auto flushPending = [&]() {
        switch (pending) {
        case ERender2dBatchKind::ScreenQuad:
            quadData->flush(ctx.cmdBuf, state);
            break;
        case ERender2dBatchKind::WorldQuad:
            quadData->flushWorld(ctx.cmdBuf, state);
            break;
        case ERender2dBatchKind::Line:
            lineData->flush(ctx.cmdBuf, state);
            break;
        case ERender2dBatchKind::None:
            break;
        }
        pending = ERender2dBatchKind::None;
    };

    for (const Render2DList::Command& command : list.commands) {
        // A command boundary is a kind change OR a clip change: the immediate
        // flusher drew each clip group with its own scissor, and merging two
        // same-kind commands with different clips into one region would apply
        // the last clip to the earlier geometry.
        const bool bBatchBoundary = command.kind != pending
            || state.bClipped != command.bClipped
            || (command.bClipped && (state.clip.pos != command.clip.pos ||
                                     state.clip.extent != command.clip.extent));
        if (bBatchBoundary) {
            flushPending();
            pending = command.kind;
        }
        state.bClipped = command.bClipped;
        state.clip     = command.clip;
        if (bDumpCommands && command.kind == ERender2dBatchKind::ScreenQuad) {
            const uint32_t ls = list.screenVerts[command.firstVertex].textureRef & FQuadRender::kTextureIndexMask;
            YA_CORE_INFO("R2D rec cmd first={} local={} -> global={} texPtr={}",
                         command.firstVertex, ls, localToGlobal[ls],
                         static_cast<const void*>(list.textures[ls].get()));
        }

        if (command.kind == ERender2dBatchKind::Line) {
            const FLineRender::Vertex* src = list.lineVerts.data() + command.firstVertex;
            uint32_t remaining = command.vertexCount;
            while (remaining > 0) {
                if (lineData->vertexCount + 2 > FLineRender::MaxVertexCount) {
                    lineData->flush(ctx.cmdBuf, state);
                }
                uint32_t room = FLineRender::MaxVertexCount - lineData->vertexCount;
                uint32_t count = std::min(room & ~1u, remaining);
                std::memcpy(lineData->vertexPtr, src, count * sizeof(FLineRender::Vertex));
                lineData->vertexPtr += count;
                lineData->vertexCount += count;
                src += count;
                remaining -= count;
            }
            continue;
        }

        const bool          bScreen = command.kind == ERender2dBatchKind::ScreenQuad;
        FQuadRender::Vertex* dst    = bScreen ? quadData->vertexPtr : quadData->worldVertexPtr;
        const FQuadRender::Vertex* src =
            (bScreen ? list.screenVerts.data() : list.worldVerts.data()) + command.firstVertex;
        uint32_t remaining = command.vertexCount;
        while (remaining > 0) {
            if (quadData->vertexCount >= FQuadRender::MaxVertexCount - 4) {
                quadData->flush(ctx.cmdBuf, state);
            }
            uint32_t room  = (FQuadRender::MaxVertexCount - quadData->vertexCount) / 4;
            uint32_t quads = std::min(room, remaining / 4);
            for (uint32_t q = 0; q < quads; ++q) {
                const uint32_t localSlot = src->textureRef & FQuadRender::kTextureIndexMask;
                if (localToGlobal[localSlot] == kUnmapped) {
                    if (quadData->textureTableFull()) {
                        // Draw what the current table covers, then re-key the
                        // rest against a fresh table.
                        flushPending();
                        quadData->resetTextureBatch();
                        std::fill(localToGlobal.begin(), localToGlobal.end(), kUnmapped);
                    }
                    localToGlobal[localSlot] =
                        quadData->findOrAddTexture(list.textures[localSlot].get()).slot;
                }
                std::memcpy(dst, src, 4 * sizeof(FQuadRender::Vertex));
                for (int vi = 0; vi < 4; ++vi) {
                    dst[vi].textureRef = (dst[vi].textureRef & ~FQuadRender::kTextureIndexMask)
                                       | localToGlobal[localSlot];
                }
                dst += 4;
                src += 4;
                remaining -= 4;
            }
            if (bScreen) {
                quadData->vertexPtr += quads * 4;
                quadData->vertexCount += quads * 4;
                quadData->indexCount += quads * 6;
            }
            else {
                quadData->worldVertexPtr += quads * 4;
                quadData->worldVertexCount += quads * 4;
                quadData->worldIndexCount += quads * 6;
            }
        }
    }
    flushPending();

    gLastFrameStats = stats;
    return stats;
}

} // namespace ya
