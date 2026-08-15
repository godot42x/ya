#pragma once

// ============================================================================
// Device-agnostic sampler descriptor enums.
//
// These are plain data enums shared by the RHI (sampler descriptions), the GUI
// texture library and gameplay material descriptors (TextureSlot). They live
// in Core so descriptor types can be defined below the RHI layer; the RHI's
// RenderDefines.h re-includes this header for backwards compatibility.
// ============================================================================

namespace ya
{

namespace EFilter
{
enum T
{
    Nearest,
    Linear,
    CubicExt,
    CubicImg,
};
} // namespace EFilter

namespace ESamplerAddressMode
{
enum T
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};
} // namespace ESamplerAddressMode

} // namespace ya
