#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/Brush.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ya
{

struct Font;

/// True when `brush` is an image (or nine-patch) meant to sit next to a
/// disclosure button. Solid tints are not icons.
[[nodiscard]] inline bool brushHasIcon(const FBrush& brush)
{
    return !brush.resource.empty();
}

/// How the expand/collapse control is drawn. Shared by UIExpander and
/// UITreeView. ASCII `>` / `v` are a Glyph option, not the chevron look —
/// those two characters are not the same mark rotated.
enum class EDisclosureKind : uint8_t
{
    /// Boxed `+` (collapsed) / `-` (expanded).
    PlusMinus,
    /// One vector chevron: collapsed points right, expanded is that same
    /// path rotated 90° clockwise in Y-down (points down).
    Chevron,
    /// Authored glyph pair (`collapsedGlyph` / `expandedGlyph`).
    Glyph,
    /// `collapsedImage`; `expandedImage` if set, otherwise the collapsed
    /// image is drawn rotated 180° (UV flip).
    Image,
    /// No expand mark. Optional leading icon + title remain; the header /
    /// row still toggles (TreeView uses the icon, or a gutter if none).
    Hidden,
};

struct FDisclosureSpec
{
    EDisclosureKind kind            = EDisclosureKind::PlusMinus;
    std::string     collapsedGlyph  = ">";
    std::string     expandedGlyph   = "v";
    FBrush          collapsedImage;
    FBrush          expandedImage;

    bool operator==(const FDisclosureSpec&) const = default;
};

[[nodiscard]] inline bool showsDisclosureButton(const FDisclosureSpec& spec)
{
    return spec.kind != EDisclosureKind::Hidden;
}

[[nodiscard]] inline const char* disclosureKindName(EDisclosureKind kind)
{
    switch (kind) {
    case EDisclosureKind::PlusMinus: return "plusMinus";
    case EDisclosureKind::Chevron:   return "chevron";
    case EDisclosureKind::Glyph:     return "glyph";
    case EDisclosureKind::Image:     return "image";
    case EDisclosureKind::Hidden:    return "hidden";
    }
    return "plusMinus";
}

/// Header leading geometry: optional disclosure slot, optional icon, then title.
struct FDisclosureLeading
{
    Rect2D button;
    Rect2D icon;
    Rect2D title;
};

[[nodiscard]] YA_GUI_API FDisclosureLeading layoutDisclosureLeading(const Rect2D& header,
                                                                    float         buttonSlotWidth,
                                                                    bool          bShowButton,
                                                                    bool          bHasIcon,
                                                                    float         iconSize = 14.0f);

struct FDisclosurePaint
{
    Rect2D            buttonRect;
    bool              bExpanded = false;
    bool              bHovered  = false;
    glm::vec4         color     = {0.60f, 0.65f, 0.70f, 1.0f};
    FBrush            hoveredFill;
    FDisclosureSpec   spec;
    std::shared_ptr<Font> font;
};

/// Shared by UIExpander and UITreeView. TreeView still paints rows itself —
/// this is not a per-row child widget.
YA_GUI_API void paintDisclosureButton(UIFrameBuilder& builder, const FDisclosurePaint& paint);

} // namespace ya
