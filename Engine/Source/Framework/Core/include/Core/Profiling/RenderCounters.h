#pragma once

#include "Core/Api.h"

#include <atomic>
#include <cstdint>

namespace ya
{

/// Per-frame GPU command counters, incremented by the backend's command
/// recording (`draw` / `dispatch` / pipeline and descriptor binds) and by the
/// 2D batch recorder (`flush` per batch, texture-table switches). The frame
/// owner resets the set at frame end; readers snapshot between frames.
///
/// Counters are the `render_counters` bench section's source of truth and the
/// editor profiling panel's quick counts. They always exist (the increments
/// are relaxed atomics on already-hot paths); only reading them is opt-in.
struct YA_CORE_API RenderCounters
{
    uint32_t drawCount              = 0;
    uint32_t drawIndexedCount       = 0;
    uint32_t indirectDrawCount      = 0;
    uint32_t dispatchCount          = 0;
    uint32_t dispatchIndirectCount  = 0;
    uint32_t pipelineBindCount      = 0;
    uint32_t descriptorBindCount    = 0;
    uint32_t batch2DCount           = 0;
    uint32_t textureTableSwitch2DCount = 0;
};

/// Record-side increments, declared as free functions so recording code does
/// not touch the struct (increment on the hot path, snapshot on the read
/// path). `indirectDraw` takes the command's draw count, not 1.
YA_CORE_API void incrementRenderCounterDraw();
YA_CORE_API void incrementRenderCounterDrawIndexed();
YA_CORE_API void incrementRenderCounterIndirectDraw(uint32_t count);
YA_CORE_API void incrementRenderCounterDispatch();
YA_CORE_API void incrementRenderCounterDispatchIndirect();
YA_CORE_API void incrementRenderCounterPipelineBind();
YA_CORE_API void incrementRenderCounterDescriptorBind();
YA_CORE_API void incrementRenderCounterBatch2D();
YA_CORE_API void incrementRenderCounterTextureTableSwitch2D();

[[nodiscard]] YA_CORE_API RenderCounters renderCountersSnapshot();
YA_CORE_API void                         resetRenderCounters();

} // namespace ya
