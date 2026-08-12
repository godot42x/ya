#pragma once

#include "RHI/RenderDefines.h"

#include <cstdint>

namespace ya
{

struct ICommandBuffer;
struct IRender;

// Internal helpers shared by the quad/line implementations of the Draw2D
// module. Defined in QuadRender.cpp; not part of the public Draw2D API.

/// Screen-space viewport + full-window scissor. Screen-space UI owns the app
/// contract (top-left origin, Y-down), but Vulkan's framebuffer coordinates
/// are Y-up. Absorb that backend detail here so layout/widgets never need to
/// care about reverse viewport.
void setScreenViewportAndScissor(ICommandBuffer& cmdBuf, IRender* render, uint32_t width, uint32_t height);

/// Default full-window viewport state sized from Render2D::session. Shared by
/// the screen/UI quad pipeline variants and the line pipeline.
ViewportState buildQuadViewportState();

} // namespace ya
