#pragma once

#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"

#include <optional>

namespace ya
{

/// Open this resource set's View slot table for the live submission token.
/// Same token is idempotent; a new token rewinds live View count.
template <typename Binding>
[[nodiscard]] inline bool beginViewBindingTable(
    RenderViewBindingTable<Binding>& views,
    const RenderSubmission&          submission)
{
    if (!submission.isRecording()) {
        return false;
    }
    return views.beginSubmission(submission.flightIndex(), submission.frameToken());
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

[[nodiscard]] inline std::optional<FrameUploadArena::Allocation> writeUploadSlice(
    RenderSubmission& submission,
    uint32_t          alignment,
    const void*       data,
    uint32_t          size)
{
    auto slice = submission.allocateUpload(size, alignment);
    if (!slice.has_value() || !slice->write(data, size)) {
        return std::nullopt;
    }
    return slice;
}

} // namespace ya
