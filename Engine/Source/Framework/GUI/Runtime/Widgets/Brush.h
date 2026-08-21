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

    bool operator==(const FBrush& o) const
    {
        return drawType == o.drawType &&
               tintColor == o.tintColor &&
               resource == o.resource &&
               margin == o.margin;
    }

    /// True when this brush is a solid fill (no image resource).
    [[nodiscard]] bool isSolid() const { return resource.empty(); }
};

} // namespace ya
