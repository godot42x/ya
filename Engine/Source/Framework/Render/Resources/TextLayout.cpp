#include "Render/Resources/TextLayout.h"

#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cmath>

namespace ya
{

namespace
{

/// The line-break candidate rules (spaces and tabs may end a line) and the
/// ignorable-code-point filter live here so both the layout walk and the
/// measurement agree.

float nextAdvance(const Font& font, uint32_t codePoint, const Character& character, glm::vec2 scale)
{
    if (codePoint == '\t') {
        return font.getCharacter(' ').advance.x * 4.0f * scale.x;
    }
    return character.advance.x * scale.x;
}

} // namespace

bool isGlyphDrawable(const Font& font, const Character& character)
{
    (void)font;
    if (character.atlasSlot == ~0u && !character.bInAtlas) {
        return false;
    }
    return character.atlasSlot != ~0u && character.size.x > 0 && character.size.y > 0
           && character.uvRect.z > 0.0f && character.uvRect.w > 0.0f;
}

std::vector<GlyphPlacement> layoutText(const TextLayoutRequest& request)
{
    std::vector<GlyphPlacement> out;
    if (request.text == nullptr || request.font == nullptr || request.text->empty()) {
        return out;
    }
    const Font&  font  = *request.font;
    const auto   codes = utf8::decode(*request.text);

    const float lineAdvance = (font.lineHeight + request.lineSpacing) * request.scale.y;
    float       penX        = 0.0f;  // along the current line, in scaled pixels
    float       lineTop     = 0.0f;  // current line's top, in scaled pixels
    bool        bLineStart  = true;

    const auto flushGlyph = [&](uint32_t codePoint, const Character& character) {
        GlyphPlacement placement;
        placement.codePoint = codePoint;
        // Top-left of the glyph box: pen + bearing x, and the line's ascent
        // line minus the glyph's top bearing (bearing.y is baseline-up).
        placement.position = request.origin + glm::vec2(penX + static_cast<float>(character.bearing.x) * request.scale.x,
                                                        lineTop + (font.ascent - static_cast<float>(character.bearing.y)) * request.scale.y);
        placement.size = glm::vec2(character.size) * request.scale;
        // Character::uvRect is (offsetU, offsetV, scaleU, scaleV); consumers
        // sample with corner UVs, so convert once here.
        placement.uvCorners = {character.uvRect.x,
                               character.uvRect.y,
                               character.uvRect.x + character.uvRect.z,
                               character.uvRect.y + character.uvRect.w};
        placement.bColorGlyph = character.bColor;
        placement.atlasIndex  = character.atlasIndex;
        placement.atlasSlot   = character.atlasSlot;
        out.push_back(placement);
    };

    for (uint32_t codePoint : codes) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n') {
            penX       = 0.0f;
            lineTop += lineAdvance;
            bLineStart = true;
            continue;
        }
        if (utf8::isIgnorableFormatCodePoint(codePoint)) {
            continue;
        }

        const Character& character = font.characters.contains(codePoint)
                                         ? font.characters.at(codePoint)
                                         : Character{};
        const float advance = nextAdvance(font, codePoint, character, request.scale);

        // Wrapping: a word that would overflow starts a new line. The break
        // is taken before a space too, so trailing spaces do not park at the
        // right edge; `bLineStart` keeps a single overflowing word from
        // looping forever.
        if (request.wrapWidthPx > 0.0f && !bLineStart && penX + advance > request.wrapWidthPx) {
            penX       = 0.0f;
            lineTop += lineAdvance;
        }

        if (codePoint == ' ' || codePoint == '\t') {
            penX += advance;
            continue;
        }

        if (font.characters.contains(codePoint) && isGlyphDrawable(font, character)) {
            flushGlyph(codePoint, character);
        }
        // A missing glyph still advances: the line lays out around it.
        penX += advance;
        bLineStart = false;
    }
    return out;
}

std::vector<GlyphPlacement> layoutText(const std::string& text, const Font& font, glm::vec2 origin, glm::vec2 scale)
{
    const std::string_view view = text;
    TextLayoutRequest request;
    request.text  = &view;
    request.font  = &font;
    request.origin = origin;
    request.scale  = scale;
    return layoutText(request);
}

glm::vec2 measureLayout(const std::string& text, const Font& font, float lineSpacing)
{
    if (text.empty() || font.characters.empty()) {
        return glm::vec2(0.0f);
    }
    const float lineAdvance = font.lineHeight + lineSpacing;

    float maxPen = 0.0f;
    float penX   = 0.0f;
    int   lines  = 1;
    for (uint32_t codePoint : utf8::decode(text)) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n') {
            maxPen = std::max(maxPen, penX);
            penX   = 0.0f;
            ++lines;
            continue;
        }
        if (utf8::isIgnorableFormatCodePoint(codePoint)) {
            continue;
        }
        const auto it = font.characters.find(codePoint);
        if (it == font.characters.end()) {
            continue;
        }
        if (codePoint == '\t') {
            penX += font.getCharacter(' ').advance.x * 4.0f;
            continue;
        }
        penX += it->second.advance.x;
    }
    maxPen = std::max(maxPen, penX);
    return glm::vec2(maxPen, static_cast<float>(lines) * lineAdvance);
}

} // namespace ya
