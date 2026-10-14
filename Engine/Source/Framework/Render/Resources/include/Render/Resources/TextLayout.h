#pragma once

#include "Core/Base.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct Font;
struct Character;

/// One laid-out glyph in font-pixel space. This is the text layer's single
/// fact about "where the glyphs are": pen positions, advances, bearings and
/// line breaks are resolved here once, and every consumer maps the result
/// into its own space (screen pixels for UI, world quad axes for 3D labels).
///
/// `position` is the glyph's top-left in the same pixel space as the pen
/// origin, y down; `size` is the glyph's pixel extent. A consumer multiplies
/// by its own scale and adds its origin (and rounds, if it draws on a pixel
/// grid).
struct GlyphPlacement
{
    /// Glyph's top-left corner, in font pixels (y down from the line's ascent top).
    glm::vec2 position{0.0f};
    /// Glyph pixel size; the atlas rect aspect, before consumer scaling.
    glm::vec2 size{0.0f};
    /// UV rect as (u0, v0, u1, v1) — corners, ready to sample.
    /// (The `Character::uvRect` member is origin+size; this is converted.)
    glm::vec4 uvCorners{0.0f};
    /// Reference into the source text, for per-glyph lookups (colour, effects).
    uint32_t codePoint = 0;
    /// Draw this glyph unmodulated (colour-emoji atlas pages).
    bool bColorGlyph = false;
    /// Index into the font's fallback chain (0 = primary face), so a consumer
    /// can ask the font which atlas/sampler the glyph came from.
    uint16_t atlasIndex = 0;
    /// The character's encoded atlas slot (page + slot), the same value
    /// `Character::atlasSlot` holds. A consumer resolves the page texture and
    /// sampler through `Font::atlasTextureFor` with this: building a fresh
    /// Character without it would decode a bogus page.
    uint32_t atlasSlot = ~0u;
};

struct TextLayoutRequest
{
    /// The text to lay out. `\n` starts a new line; `\t` advances four spaces.
    const std::string_view* text = nullptr;
    const Font*             font = nullptr;
    /// Origin of the first line's pen (its baseline-left is `origin + (0, ascent)`).
    glm::vec2 origin{0.0f};
    /// Per-axis scale applied to advances and bearings while laying out. A
    /// consumer that will scale the whole block uniformly passes 1 here and
    /// scales the returned rects; a consumer that must lay out at the final
    /// size (so lines break at the right width) passes its real scale.
    glm::vec2 scale{1.0f};
    /// Extra advance between lines, in pixels, on top of `font->lineHeight`.
    float lineSpacing = 0.0f;
    /// Bounds the layout: a glyph whose right edge would exceed this width
    /// starts a new line. Zero disables wrapping.
    float wrapWidthPx = 0.0f;
};

/// Lay `request.text` out into placed glyphs. This is the ONE implementation
/// of the layout walk: advance accumulation, bearings, the ascent baseline,
/// space/tab widths, newlines and (opt) wrapping.
///
/// A code point the font has no usable atlas entry for advances the pen and
/// produces no placement, so a line lays out around a missing glyph instead
/// of collapsing. Placements come back in reading order; a consumer never
/// re-implements "what does this code point mean".
[[nodiscard]] YA_RENDER_RESOURCES_API std::vector<GlyphPlacement> layoutText(const TextLayoutRequest& request);

/// Convenience: `layoutText` for a whole string, no wrapping.
[[nodiscard]] YA_RENDER_RESOURCES_API std::vector<GlyphPlacement> layoutText(const std::string& text,
                                                                             const Font&        font,
                                                                             glm::vec2          origin = {},
                                                                             glm::vec2          scale  = {1.0f, 1.0f});

/// Total laid-out size in font pixels: `(max pen X, line count * lineHeight)`.
/// Mirrors what `layoutText` places, so a consumer can centre or right-align
/// a block without re-walking the string. Includes `lineSpacing`.
[[nodiscard]] YA_RENDER_RESOURCES_API glm::vec2 measureLayout(const std::string& text,
                                                              const Font&        font,
                                                              float              lineSpacing = 0.0f);

/// Whether `font` has a usable atlas entry for `codePoint` (the same rule
/// `layoutText` uses to decide skippable glyphs). For callers that branch on
/// glyph availability before laying out (substituting a placeholder string).
[[nodiscard]] YA_RENDER_RESOURCES_API bool isGlyphDrawable(const Font& font, const Character& character);

} // namespace ya
