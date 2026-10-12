#include "Render3D/Common/Text2DExtraction.h"

#include "Render/Resources/FontManager.h"
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

/// The font atlas stores a glyph as (offsetU, offsetV, scaleU, scaleV); a
/// sprite candidate wants two corners.
glm::vec4 uvCorners(const glm::vec4& fontRect)
{
    return glm::vec4(fontRect.x, fontRect.y, fontRect.x + fontRect.z, fontRect.y + fontRect.w);
}

/// Line width in font pixels, matching what the glyph layout below will place.
float linePixelWidth(const Font& font, std::string_view line)
{
    float width = 0.0f;
    const float tabWidth = font.getCharacter(' ').advance.x * 4.0f;
    for (uint32_t codePoint : utf8::decode(line)) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\t') {
            width += tabWidth;
            continue;
        }
        width += font.getCharacter(codePoint).advance.x;
    }
    return width;
}

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
        const Character& character = found->second;
        if (character.atlasSlot != ~0u && character.size.x > 0 && character.size.y > 0
            && character.uvRect.z > 0.0f && character.uvRect.w > 0.0f) {
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

    // Split into lines first so the block can be measured for the pivot shift.
    std::vector<std::string> lines;
    {
        size_t start = 0;
        const std::string& text = label.text;
        while (true) {
            const size_t nl = text.find('\n', start);
            lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
    }

    float blockWidth = 0.0f;
    for (const std::string& line : lines) {
        blockWidth = std::max(blockWidth, linePixelWidth(font, line));
    }
    const float blockHeight = static_cast<float>(lines.size()) * linePixelHeight;

    // The pivot sits on the entity: the block's top-left is offset back from
    // the anchor by the pivot fraction of the block size.
    const float topLeftLocalX = -blockWidth * label.pivot.x * worldPerPixel;
    const float topLeftLocalY = blockHeight * label.pivot.y * worldPerPixel;

    const glm::vec3 right = glm::vec3(in.world[0]);
    const glm::vec3 up    = glm::vec3(in.world[1]);
    const glm::vec3 origin = glm::vec3(in.world[3]);

    const float tabWidth = font.getCharacter(' ').advance.x * 4.0f;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        float cursorX = 0.0f;
        const float baselineTop = static_cast<float>(lineIndex) * linePixelHeight;
        for (uint32_t codePoint : utf8::decode(lines[lineIndex])) {
            if (codePoint == '\r') {
                continue;
            }
            if (codePoint == '\t') {
                cursorX += tabWidth;
                continue;
            }
            // Read the glyph straight from the map, not through getCharacter:
            // getCharacter substitutes '?' for a codepoint the font has not
            // rasterized yet, which would draw a wrong glyph for a frame. A
            // missing glyph is skipped; it appears once the atlas has it.
            const auto found = font.characters.find(codePoint);
            if (found == font.characters.end()) {
                continue;
            }
            const Character& character = found->second;
            if (codePoint == ' ') {
                cursorX += character.advance.x;
                continue;
            }
            if (character.atlasSlot == ~0u || character.size.x <= 0 || character.size.y <= 0
                || character.uvRect.z <= 0.0f || character.uvRect.w <= 0.0f) {
                cursorX += character.advance.x;
                continue;
            }
            std::shared_ptr<Texture> page = font.atlasTextureFor(character);
            if (!page) {
                cursorX += character.advance.x;
                continue;
            }

            const float left = cursorX + static_cast<float>(character.bearing.x);
            const float top  = baselineTop + font.ascent - static_cast<float>(character.bearing.y);
            const float width  = static_cast<float>(character.size.x);
            const float height = static_cast<float>(character.size.y);
            cursorX += character.advance.x;

            // Glyph centre in local units (x right, y up), relative to the anchor.
            const float centerX = topLeftLocalX + (left + width * 0.5f) * worldPerPixel;
            const float centerY = topLeftLocalY - (top + height * 0.5f) * worldPerPixel;

            WorldSpriteCandidate candidate{};
            candidate.worldCenter = origin + right * centerX + up * centerY;
            candidate.axisX = right * (width * worldPerPixel);
            candidate.axisY = up * (height * worldPerPixel);
            candidate.uvRect = uvCorners(character.uvRect);
            candidate.tint = label.color;
            candidate.texture.setTexture(page);
            candidate.texture.setSampler(in.resolveSampler(*page));
            candidate.sortPointY = candidate.worldCenter.y;
            candidate.drawKey = makeSpriteDrawKey(label.layer, label.bYSort, candidate.sortPointY,
                                                  label.sortOrder, in.entityId, 0);
            out.push_back(std::move(candidate));
        }
    }
}

} // namespace ya
