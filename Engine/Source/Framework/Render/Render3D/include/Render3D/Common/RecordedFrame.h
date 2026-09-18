#pragma once

#include <cstdint>

namespace ya
{

struct ICommandBuffer;

/// What one completed recording produced: the command buffer the host submits,
/// and the identity of the recording it belongs to.
///
/// The recording result is a value rather than a bare command-buffer pointer
/// because "did anything get recorded" and "which recording is this" are the
/// questions the host actually asks. A null command buffer is not an error: it
/// means the recording was refused (unpresentable surface, no live submission,
/// or a recording that could not be sealed), and the host then submits an empty
/// frame, which still legalizes the acquired image.
///
/// flightIndex and frameToken are that recording's identity, not new state to
/// store: they name the fence slot and the submission serial that the kept
/// resources are bound to.
struct RecordedFrame
{
    ICommandBuffer* commandBuffer = nullptr;
    uint32_t        flightIndex   = 0;
    uint64_t        frameToken    = 0;

    [[nodiscard]] bool valid() const { return commandBuffer != nullptr; }
};

} // namespace ya
