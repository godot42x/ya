#pragma once

#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderViewBindingTable.h"

#include <optional>

namespace ya
{

/// Open a flight on the submission arena and the View binding table together.
/// Same `(flightIndex, frameToken)` is idempotent on both sides.
template <typename Binding>
[[nodiscard]] inline bool beginFrameResourceSubmission(
    FrameUploadArena*                arena,
    RenderViewBindingTable<Binding>& views,
    const RenderSubmissionContext&   submission)
{
    if (!arena || submission.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }
    if (!arena->beginFlight(submission.flightIndex, submission.frameToken)) {
        return false;
    }
    return views.beginSubmission(submission.flightIndex, submission.frameToken);
}

[[nodiscard]] inline std::optional<FrameUploadArena::Allocation> writeUploadSlice(
    FrameUploadArena& arena,
    uint32_t          flightIndex,
    uint32_t          alignment,
    const void*       data,
    uint32_t          size)
{
    if (alignment == 0 || data == nullptr || size == 0) {
        return std::nullopt;
    }
    auto slice = arena.allocate(flightIndex, size, alignment);
    if (!slice.has_value() || !slice->write(data, size)) {
        return std::nullopt;
    }
    return slice;
}

} // namespace ya
