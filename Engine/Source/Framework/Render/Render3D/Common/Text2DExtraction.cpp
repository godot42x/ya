#include "Render3D/Common/Text2DExtraction.h"

#include "Render/Resources/FontManager.h"
#include "Render/Resources/TextLayout.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Core/Texture.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

namespace
{

} // namespace

bool fontHasAnyGlyph(const Font& font, const std::string& text)
{
    for (uint32_t codePoint : utf8::decode(text)) {
        if (codePoint == '\r' || codePoint == '\n' || codePoint == '\t' || codePoint == ' ') {
            continue;
        }
        const auto found = font.characters.find(codePoint);
        if (found == font.characters.end()) {
            continue;
        }
        if (isGlyphDrawable(font, found->second)) {
            return true;
        }
    }
    return false;
}

void appendText2DCandidates(const Text2DExtractionInput& in, std::vector<WorldSpriteCandidate>& out)
{
    if (!in.component || !in.font || !in.resolveSampler) {
        return;
    }
    const Text2DComponent& label = *in.component;
    if (!label.bVisible || label.text.empty()) {
        return;
    }
    const Font& font = *in.font;
    if (font.characters.empty()) {
        return;
    }

    // Pixels -> local world units. `size` is the height of one line, so the
    // line height is the reference; a font with no metrics falls back to its
    // nominal size.
    float linePixelHeight = font.lineHeight > 0.0f ? font.lineHeight : static_cast<float>(font.fontSize);
    if (linePixelHeight <= 0.0f) {
        return;
    }
    const float worldPerPixel = in.lineWorldHeight / linePixelHeight;

    // The layout walk is shared with screen text (Render/Resources/TextLayout):
    // one implementation of advances, bearings, baselines and wrapping. This
    // path only maps the placed glyphs into world quads.
    const std::vector<GlyphPlacement> glyphs = layoutText(label.text, font);
    if (glyphs.empty()) {
        return;
    }

    // The pivot sits on the entity: the block's top-left is offset back from
    // the anchor by the pivot fraction of the block size. The block's own
    // size comes from the same measurement the layout used.
    const glm::vec2 blockPx = measureLayout(label.text, font);
    const float topLeftLocalX = -blockPx.x * label.pivot.x * worldPerPixel;
    const float topLeftLocalY = blockPx.y * label.pivot.y * worldPerPixel;

    const glm::vec3 right  = glm::vec3(in.world[0]);
    const glm::vec3 up     = glm::vec3(in.world[1]);
    const glm::vec3 origin = glm::vec3(in.world[3]);

    // Outline pass: the same glyph quad, tinted and shifted. This is the
    // coverage-stamp border a bitmap font atlas gets (Godot's Label3D
    // outline_size, TMP's outline): the glyph's own coverage is stamped around
    // the ink, so the border follows the glyph shape rather than a box.
    //
    // Four axis offsets, not eight: the glyph quads are axis-aligned and the
    // coverage stamp is square, so a diagonal offset only adds corners that the
    // four cardinals already reach.
    //
    // `outlineWidth` is in font-raster pixels, the same unit as
    // `fontPixelSize`, and converts through the same `worldPerPixel` the glyph
    // sizes use -- so the border scales with the text and never depends on the
    // scene's texel grid.
    const float outline = label.outlineWidth > 0.0f ? label.outlineWidth * worldPerPixel : 0.0f;
    const bool  bOutline = outline > 0.0f;
    static constexpr glm::vec2 kOutlineOffsets[4] = {
        {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
    };

    for (const GlyphPlacement& glyph : glyphs) {
        // The page comes from the glyph's encoded slot: a Character built
        // without it decodes page 0/~0 and yields an empty texture.
        Character lookup{};
        lookup.atlasSlot  = glyph.atlasSlot;
        lookup.atlasIndex = glyph.atlasIndex;
        lookup.bInAtlas   = true;
        const std::shared_ptr<Texture> page = font.atlasTextureFor(lookup);
        if (!page) {
            continue;
        }

        // Glyph centre in local units (x right, y up), relative to the anchor.
        const float centerX = topLeftLocalX + (glyph.position.x + glyph.size.x * 0.5f) * worldPerPixel;
        const float centerY = topLeftLocalY - (glyph.position.y + glyph.size.y * 0.5f) * worldPerPixel;
        const glm::vec3 center = origin + right * centerX + up * centerY;

        const auto emit = [&](const glm::vec3& at, const glm::vec4& tintColor, uint32_t sequence) {
            WorldSpriteCandidate candidate{};
            candidate.worldCenter = at;
            candidate.axisX       = right * (glyph.size.x * worldPerPixel);
            candidate.axisY       = up * (glyph.size.y * worldPerPixel);
            candidate.uvRect      = glyph.uvCorners;
            candidate.tint        = tintColor;
            candidate.texture.setTexture(page);
            if (in.resolveSampler) {
                candidate.texture.setSampler(in.resolveSampler(*page));
            }
            // The sort point is the glyph's own centre, not the shifted quad's:
            // y-sorting must see one y per glyph, or the border quads scatter
            // around the ink instead of sitting under it.
            candidate.sortPointY = center.y;
            // Text keeps the layout engine's measured positions: the tilemap
            // texel grid is the pixel-art grid, not the glyph grid, and snapping
            // to it would quantize the advances and baselines.
            candidate.bSnapToTexelGrid = false;
            candidate.drawKey = makeSpriteDrawKey(label.layer, label.bYSort, candidate.sortPointY,
                                                  label.sortOrder, in.entityId, sequence);
            out.push_back(std::move(candidate));
        };

        if (bOutline) {
            for (const glm::vec2& offset : kOutlineOffsets) {
                emit(center + right * (offset.x * outline) + up * (offset.y * outline),
                     label.outlineColor, 0);
            }
        }
        emit(center, glyph.bColorGlyph ? glm::vec4(1.0f) : label.color, 1);
    }
}

} // namespace ya
