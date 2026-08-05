#pragma once

#include "Runtime/Rendering/Common/RenderOverlay.h"

#include <memory>

namespace ya
{

struct FrameContext;
struct ICommandBuffer;

void recordRenderViewportOverlayPass(const FrameContext& frameCtx,
                                     const std::shared_ptr<const RenderViewportOverlaySnapshot>& overlaySnapshot,
                                     ICommandBuffer* cmdBuf);

} // namespace ya
