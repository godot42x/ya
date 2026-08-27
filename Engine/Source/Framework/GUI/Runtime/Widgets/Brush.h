#pragma once

// ============================================================================
// Brush - a unified visual primitive that carries BOTH solid color and image
// (style-system brush abstraction, UE FSlateBrush / Godot StyleBox lineage).
//
// A solid fill is the degenerate form (no resource + tint colors the white
// sprite); an image is resource + tint. Nine-patch slices a resource into 9
// regions via `margin` so corners keep their size while edges/center stretch.
//
// Pure data: the asset path is resolved to a strong texture at paint time via
// UIFrameBuildContext::textureResolver (widgets never reach the asset layer).
// ============================================================================

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include <glm/glm.hpp>

#include <string>

namespace ya
{

struct FBrush
{
    enum class EDrawType : uint8_t
    {
        Image,     // stretch the whole resource (or solid tint when no resource)
        NinePatch, // slice into 9 regions by margin: corners fixed, edges/center stretch
        Border,    // nine-patch without the center region (frame only)
    };

    EDrawType   drawType  = EDrawType::Image;
    glm::vec4   tintColor = {1.0f, 1.0f, 1.0f, 1.0f}; // color modulation; solid = no resource + tint
    std::string resource;                             // asset path; empty = solid fill
    glm::vec4   margin = {0.0f, 0.0f, 0.0f, 0.0f};    // nine-patch insets (left/top/right/bottom, texture px)

    // Compiler-generated memberwise equality: adding/removing a field can
    // never silently desync == (which would make Reactive::set() short-circuit
    // a real change and skip repaint).
    bool operator==(const FBrush&) const = default;

    /// True when this brush is a solid fill (no image resource).
    [[nodiscard]] bool isSolid() const { return resource.empty(); }

    /// Convenience factory: a solid brush tinted with `color` (no resource).
    static FBrush Solid(const glm::vec4& color)
    {
        FBrush b;
        b.tintColor = color;
        return b;
    }

    /// Convenience factory: an image brush from an asset path (white tint so
    /// the texture shows unmodulated; pass a tint to colorize it).
    static FBrush Image(const std::string& assetPath, const glm::vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        FBrush b;
        b.resource  = assetPath;
        b.tintColor = tint;
        return b;
    }

    /// Nine-patch: corners keep `margin` texture px, edges/center stretch.
    /// `margin` is left/top/right/bottom in texture pixels (1 tex px = 1 logical px).
    static FBrush NinePatch(const std::string& assetPath,
                            const glm::vec4&   margin,
                            const glm::vec4&   tint = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        FBrush b;
        b.drawType  = EDrawType::NinePatch;
        b.resource  = assetPath;
        b.margin    = margin;
        b.tintColor = tint;
        return b;
    }

    /// Border: nine-patch without the center fill (frame only).
    static FBrush Border(const std::string& assetPath,
                         const glm::vec4&   margin,
                         const glm::vec4&   tint = {1.0f, 1.0f, 1.0f, 1.0f})
    {
        FBrush b;
        b.drawType  = EDrawType::Border;
        b.resource  = assetPath;
        b.margin    = margin;
        b.tintColor = tint;
        return b;
    }
};

/// One nine-patch / image cell: destination in logical px, UV origin + size.
struct FBrushSlice
{
    Rect2D    dest{};
    glm::vec2 uvOffset{0.0f, 0.0f};
    glm::vec2 uvScale{1.0f, 1.0f};
};

inline constexpr int kMaxBrushSlices = 9;

/// Slice `dest` into Image (1) / NinePatch (up to 9) / Border (up to 8) cells.
/// `texturePx` is the resource size in texture pixels; zero or a solid brush
/// yields a single full-rect slice (stretch / solid fill).
YA_GUI_API int sliceBrush(const FBrush& brush, const Rect2D& dest, glm::vec2 texturePx, FBrushSlice out[kMaxBrushSlices]);

} // namespace ya
