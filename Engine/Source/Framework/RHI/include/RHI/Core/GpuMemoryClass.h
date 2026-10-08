#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ya
{

/// Which accounting bucket a GPU allocation is charged to. `IRender`
/// reports memory per class, and the benchmark summary and the editor panel
/// break the device budget down along the same line.
///
/// `Auto` means "the creator did not say": the backend derives the bucket from
/// how the resource is used (see `resolveGpuMemoryClass`). Callers that know
/// better than the heuristic -- the render graph's transient attachments, for
/// one -- state it at creation time instead of relying on it.
namespace EGpuMemoryClass
{
enum T : uint8_t
{
    Auto = 0,
    /// Images sampled by shaders: material textures, font atlases, imported
    /// content. Uploaded once, read many times.
    Texture,
    /// Images rendered into and kept across frames: GBuffer, shadow maps,
    /// scene color, the offscreen target a window presents.
    RenderTarget,
    /// Images whose content only has to survive inside one frame's graph
    /// execution, so their backing memory is a candidate for aliasing.
    Transient,
    /// Device-resident buffers: vertex, index, storage, uniform.
    Buffer,
    /// Host-visible buffers the CPU writes: staging, streaming, dynamic UBOs.
    Upload,
};
} // namespace EGpuMemoryClass

/// Buckets an allocation can actually land in: every class but `Auto`.
inline constexpr uint32_t kGpuMemoryClassCount = 5;

/// The buckets in array order, so per-class tables can be built and walked
/// without a second switch.
inline constexpr std::array<EGpuMemoryClass::T, kGpuMemoryClassCount> kGpuMemoryClasses{
    EGpuMemoryClass::Texture,
    EGpuMemoryClass::RenderTarget,
    EGpuMemoryClass::Transient,
    EGpuMemoryClass::Buffer,
    EGpuMemoryClass::Upload,
};

/// Slot of `memoryClass` in the per-class tables. `Auto` is not a bucket:
/// resolve it before charging anything to one.
[[nodiscard]] inline constexpr uint32_t gpuMemoryClassIndex(EGpuMemoryClass::T memoryClass)
{
    return static_cast<uint32_t>(memoryClass) - 1u;
}

/// Summary / UI label of a class.
[[nodiscard]] inline constexpr std::string_view toGpuMemoryClassLabel(EGpuMemoryClass::T memoryClass)
{
    switch (memoryClass) {
    case EGpuMemoryClass::Auto:
        return "auto";
    case EGpuMemoryClass::Texture:
        return "texture";
    case EGpuMemoryClass::RenderTarget:
        return "renderTarget";
    case EGpuMemoryClass::Transient:
        return "transient";
    case EGpuMemoryClass::Buffer:
        return "buffer";
    case EGpuMemoryClass::Upload:
        return "upload";
    }
    return "unknown";
}

} // namespace ya
