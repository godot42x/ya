#pragma once

#include "Render/Core/CommandBuffer.h"

namespace ya
{

/// Records a viewport covering `width` x `height` plus a matching scissor.
/// `bReverseY` flips the viewport vertically for top-left-origin passes.
inline void setViewportAndScissor(ICommandBuffer& cmdBuf, uint32_t width, uint32_t height, bool bReverseY)
{
    float viewportY      = 0.0f;
    float viewportHeight = static_cast<float>(height);
    if (bReverseY) {
        viewportY      = static_cast<float>(height);
        viewportHeight = -static_cast<float>(height);
    }
    cmdBuf.setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
    cmdBuf.setScissor(0, 0, width, height);
}

} // namespace ya
