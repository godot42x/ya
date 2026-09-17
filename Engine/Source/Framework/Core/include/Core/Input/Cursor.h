#pragma once

#include <cstdint>

namespace ya
{

/// System mouse cursor requested by widgets / input nodes. The native
/// implementation lives in Core/Os (`OsCursor`); this enum is window-system
/// agnostic so GUI widgets do not depend on the OS backend.
enum class ECursorType : uint8_t
{
    Arrow,            // default pointer
    IBeam,            // text insertion caret
    ResizeEastWest,   // vertical divider (left/right panes)
    ResizeNorthSouth, // horizontal divider (top/bottom panes)
};

} // namespace ya
