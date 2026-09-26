#include "Render2D/Render2D.h"

#include "Core/Log.h"
#include "RHI/Core/CommandBuffer.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ya
{

FRender2dDebugState Render2D::debug;
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

Rect2D Render2D::intersectClipRect(const Rect2D& rect, const Rect2D& parentClip)
{
    return intersectClipRect(rect, parentClip);
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
