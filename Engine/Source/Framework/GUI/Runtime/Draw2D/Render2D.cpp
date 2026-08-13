#include "Render2D.h"

#include "Core/Log.h"

namespace ya
{

FRender2dDebugState Render2D::debug;
FRender2dSession    Render2D::session;
FQuadRender*        Render2D::quadData = nullptr;
FLineRender*        Render2D::lineData = nullptr;

void Render2D::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    quadData = new FQuadRender();
    quadData->init(render, colorFormat, depthFormat);

    lineData = new FLineRender();
    lineData->init(render, colorFormat, depthFormat);
}

void Render2D::destroy()
{
    lineData->destroy();
    delete lineData;
    lineData = nullptr;

    quadData->destroy();
    delete quadData;
    quadData = nullptr;
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
    session.debugClipLogCount    = 0;
    session.debugScreenFlushCount = 0;
    session.debugWorldFlushCount  = 0;
    if (debug.bLogSessionLifecycle) {
        YA_CORE_INFO("Render2D begin: passSlot={} extent={}x{} cmdBuf={} reverseViewport={}",
                     static_cast<size_t>(ctx.passSlot),
                     ctx.windowWidth,
                     ctx.windowHeight,
                     static_cast<const void*>(ctx.cmdBuf),
                     debug.bReverseViewport);
    }
    Extent2D extent{.width = session.windowWidth, .height = session.windowHeight};
    quadData->begin(ctx.passSlot, extent);
    lineData->begin(ctx.passSlot);
}

void Render2D::end()
{
    quadData->end();
    lineData->flush(session.curCmdBuf, session.viewProjection);

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
    static Render2DPassSlot sNextSlot = 0;
    const Render2DPassSlot  slot      = sNextSlot++;
    YA_CORE_ASSERT(slot < FQuadRender::kMaxPassSlots,
                   "Render2D pass slot pool exhausted ({} slots)",
                   FQuadRender::kMaxPassSlots);
    return slot;
}

void Render2D::preparePassPipeline(Render2DPassSlot passSlot, EFormat::T colorFormat, EFormat::T depthFormat)
{
    if (quadData) {
        quadData->preparePassPipeline(passSlot, colorFormat, depthFormat);
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
    if (bClipChanged && quadData && session.curCmdBuf) {
        // Flush pending quads with the CURRENT scissor BEFORE switching to the
        // new clip; otherwise content recorded outside the clip gets culled by
        // the incoming clip rect.
        quadData->flush(session.curCmdBuf);
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
    if (quadData && session.curCmdBuf) {
        // Flush pending quads with the CURRENT (inner) scissor BEFORE popping;
        // otherwise content recorded inside the clip escapes it.
        quadData->flush(session.curCmdBuf);
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

} // namespace ya
