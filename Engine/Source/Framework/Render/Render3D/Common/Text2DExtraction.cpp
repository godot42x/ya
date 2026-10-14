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

        WorldSpriteCandidate candidate{};
        candidate.worldCenter = origin + right * centerX + up * centerY;
        candidate.axisX       = right * (glyph.size.x * worldPerPixel);
        candidate.axisY       = up * (glyph.size.y * worldPerPixel);
        candidate.uvRect      = glyph.uvCorners;
        candidate.tint        = glyph.bColorGlyph ? glm::vec4(1.0f) : label.color;
        candidate.texture.setTexture(page);
        if (in.resolveSampler) {
            candidate.texture.setSampler(in.resolveSampler(*page));
        }
        candidate.sortPointY = candidate.worldCenter.y;
        candidate.drawKey = makeSpriteDrawKey(label.layer, label.bYSort, candidate.sortPointY,
                                              label.sortOrder, in.entityId, 0);
        out.push_back(std::move(candidate));
    }
}

} // namespace ya
