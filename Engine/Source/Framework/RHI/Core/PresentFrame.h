#pragma once

#include "RHI/Core/RenderSurfaceContext.h"

#include <cstdint>
#include <vector>

namespace ya
{

/// One acquired present attempt on a single `IRenderSurfaceContext`.
/// Recreate / zero-extent / minimize stay inside `begin()`; this is only the
/// host/coordinator pairing of acquire → record → present.
struct FPresentFrame
{
    IRenderSurfaceContext* surface    = nullptr;
    int32_t                imageIndex = -1;

    [[nodiscard]] bool acquired() const { return surface != nullptr && imageIndex >= 0; }
};

/// Wait in-flight, apply dirty recreate, acquire. `acquired()` is false when
/// the surface is unpresentable (`imageIndex == -1`); that is not a failed
/// begin. Returns false only when acquire itself failed.
[[nodiscard]] inline bool acquirePresentFrame(FPresentFrame& frame)
{
    frame.imageIndex = -1;
    if (!frame.surface) {
        return false;
    }
    return frame.surface->begin(&frame.imageIndex);
}

/// Submit recorded command buffers and present this surface. Empty command
/// list still legalizes an acquired image. `imageIndex < 0` is a no-op end.
inline bool submitPresentFrame(FPresentFrame& frame, std::vector<void*> commandBuffers)
{
    if (!frame.surface) {
        return false;
    }
    const int32_t imageIndex = frame.imageIndex;
    frame.imageIndex         = -1;
    return frame.surface->end(imageIndex, std::move(commandBuffers));
}

} // namespace ya
