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

FQuadRender::FRender2dFrameStats gLastFrameStats{};

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

} // namespace

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
    };

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

        // Screen and world quads differ in typed vertex layout, so the copy
        // paths are spelled out. Each quad's textureSlot is re-keyed into the
        // pass's global binding table; sampleMode is typed data carried per
        // quad and never part of the table. A capacity flush closes the
        // current region and continues the same command in a fresh region;
        // `emitted` counts vertices already copied for THIS command.
        if (command.kind == ERender2dBatchKind::ScreenQuad) {
            uint32_t emitted = 0;
            while (emitted < command.vertexCount) {
                if (quadData->vertexCount >= FQuadRender::MaxVertexCount - 4) {
                    quadData->flush(ctx.cmdBuf, state);
                }
                const uint32_t quads = std::min(
                    static_cast<uint32_t>((FQuadRender::MaxVertexCount - quadData->vertexCount) / 4),
                    (command.vertexCount - emitted) / 4);
                for (uint32_t q = 0; q < quads; ++q) {
                    const uint32_t srcIndex = command.firstVertex + emitted + q * 4;
                    const uint32_t localSlot = list.screenVerts[srcIndex].textureSlot;
                    if (localToGlobal[localSlot] == kUnmapped) {
                        if (quadData->textureTableFull()) {
                            flushPending();
                            quadData->resetTextureBatch();
                            std::fill(localToGlobal.begin(), localToGlobal.end(), kUnmapped);
                        }
                        localToGlobal[localSlot] = quadData->findOrAddTexture(list.textures[localSlot].get());
                    }
                    for (int vi = 0; vi < 4; ++vi) {
                        ScreenVertex v = list.screenVerts[srcIndex + vi];
                        v.textureSlot = localToGlobal[localSlot];
                        quadData->vertexPtr[vi] = v;
                    }
                    quadData->vertexPtr += 4;
                    quadData->vertexCount += 4;
                    quadData->indexCount += 6;
                }
                emitted += quads * 4;
            }
        }
    }
    flushPending();

    gLastFrameStats = stats;
    return stats;
}

} // namespace ya
