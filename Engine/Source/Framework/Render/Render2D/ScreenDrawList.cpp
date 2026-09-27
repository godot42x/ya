#include "Render2D/ScreenDrawList.h"

#include "Render/Resources/FontManager.h"

#include <algorithm>

namespace ya
{

namespace
{

constexpr std::array<glm::vec4, 4> kUnitCorners = {{
    {0.0f, 0.0f, 0.0f, 1.f},
    {1.0f, 0.0f, 0.0f, 1.f},
    {0.0f, 1.0f, 0.0f, 1.f},
    {1.0f, 1.0f, 0.0f, 1.f},
}};
constexpr std::array<glm::vec2, 4> kUnitUv = {{
    {0, 0},
    {1, 0},
    {0, 1},
    {1, 1},
}};

void emitQuad(ScreenVertex*                   out,
              const ScreenAffine&             transform,
              uint32_t                        textureSlot,
              uint32_t                        sampleMode,
              const std::array<glm::vec4, 4>& colors,
              const glm::vec2&                uvScale,
              const glm::vec2&                uvTranslation,
              const glm::vec3&                corner)
{
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 xy = transform.origin
            + transform.xAxis * kUnitCorners[static_cast<size_t>(i)].x
            + transform.yAxis * kUnitCorners[static_cast<size_t>(i)].y;
        out[i] = ScreenVertex{
            .pos         = {xy.x, xy.y, transform.z},
            .color       = colors[static_cast<size_t>(i)],
            .texCoord    = kUnitUv[static_cast<size_t>(i)] * uvScale + uvTranslation,
            .textureSlot = textureSlot,
            .sampleMode  = sampleMode,
            .corner      = corner,
        };
    }
}

} // namespace

void ScreenDrawList::closePendingCommand()
{
    if (pendingCount == 0) {
        bPending = false;
        return;
    }
    commands.push_back(Command{
        .firstVertex = pendingFirst,
        .vertexCount = pendingCount,
        .bClipped    = !clipStack.empty(),
        .clip        = clipStack.empty() ? Rect2D{} : clipStack.back(),
    });
    ++commandCount;
    pendingCount = 0;
    bPending     = false;
}

uint32_t ScreenDrawList::findOrAddTexture(const Ptr<Texture>& texture)
{
    uint32_t textureIdx = 0;
    auto it = texturePtr2Idx.find(texture.get());
    if (it != texturePtr2Idx.end()) {
        textureIdx = it->second;
    }
    else {
        textures.push_back(texture);
        textureIdx = static_cast<uint32_t>(textures.size() - 1);
        texturePtr2Idx.emplace(texture.get(), textureIdx);
    }
    return textureIdx;
}

void ScreenDrawList::appendQuad(const ScreenAffine&             transform,
                                uint32_t                        textureSlot,
                                uint32_t                        sampleMode,
                                const std::array<glm::vec4, 4>& colorsYaOrder,
                                const glm::vec2&                uvScale,
                                const glm::vec2&                uvTranslation,
                                const glm::vec3&                corner)
{
    if (!bPending) {
        bPending     = true;
        pendingFirst = static_cast<uint32_t>(vertices.size());
        pendingCount = 0;
    }
    vertices.resize(vertices.size() + 4);
    emitQuad(vertices.data() + vertices.size() - 4,
             transform, textureSlot, sampleMode,
             colorsYaOrder, uvScale, uvTranslation, corner);
    pendingCount += 4;
}

void ScreenDrawList::makeSprite(const glm::vec3& position,
                                const glm::vec2& size,
                                Ptr<Texture>     texture,
                                const glm::vec4& tint,
                                const glm::vec2& uvScale,
                                const glm::vec2& uvOffset,
                                bool             bOpaqueSample)
{
    makeSprite(screenAffineFromPositionSize(position, size),
               texture, tint, uvScale, uvOffset, bOpaqueSample);
}

void ScreenDrawList::makeSprite(const ScreenAffine& transform,
                                Ptr<Texture>        texture,
                                const glm::vec4&    tint,
                                const glm::vec2&    uvScale,
                                const glm::vec2&    uvOffset,
                                bool                bOpaqueSample)
{
    appendQuad(transform,
               findOrAddTexture(texture),
               static_cast<uint32_t>(bOpaqueSample ? EScreenTextureSampleMode::Opaque
                                                   : EScreenTextureSampleMode::Coverage),
               {tint, tint, tint, tint}, uvScale, uvOffset, {0.0f, 0.0f, 0.0f});
}

void ScreenDrawList::appendSubTexture(const glm::vec3&    position,
                                      const glm::vec2&    size,
                                      const Ptr<Texture>& texture,
                                      const glm::vec4&    tint,
                                      const glm::vec4&    uvRect,
                                      uint32_t            sampleMode)
{
    appendQuad(screenAffineFromPositionSize(position, size),
               findOrAddTexture(texture),
               sampleMode,
               {tint, tint, tint, tint}, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y},
               {0.0f, 0.0f, 0.0f});
}

void ScreenDrawList::makeText(const std::string& text,
                              const glm::vec3&   position,
                              const glm::vec4&   color,
                              Font*              font,
                              const glm::vec2&   scale)
{
    YA_CORE_ASSERT(font != nullptr, "ScreenDrawList::makeText called with a null font");

    float cursorX = position.x;
    float cursorY = position.y;

    const auto codePoints = utf8::decode(text);
    for (uint32_t codePoint : codePoints) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n') {
            cursorX = position.x;
            cursorY += font->lineHeight * scale.y;
            continue;
        }
        if (utf8::isIgnorableFormatCodePoint(codePoint)) {
            continue;
        }

        const Character& character = font->getCharacter(codePoint);
        if (codePoint == ' ') {
            cursorX += character.advance.x * scale.x;
            continue;
        }
        if (codePoint == '\t') {
            cursorX += font->getCharacter(' ').advance.x * 4.0f * scale.x;
            continue;
        }

        if (character.atlasSlot == ~0u || character.size.x <= 0 || character.size.y <= 0) {
            cursorX += character.advance.x * scale.x;
            continue;
        }

        float xpos = cursorX + static_cast<float>(character.bearing.x) * scale.x;
        float ypos = cursorY + static_cast<float>(font->ascent - character.bearing.y) * scale.y;
        xpos = std::round(xpos);
        ypos = std::round(ypos);
        const glm::vec3 pos = glm::vec3(xpos, ypos, position.z);
        const glm::vec2 scaledGlyphSize = glm::round(glm::vec2(character.size) * scale);

        const auto atlasTexture = font->atlasTextureFor(character);
        if (atlasTexture) {
            const auto sampleMode = font->renderModeFor(character) == EFontRenderMode::SDF
                                        ? EScreenTextureSampleMode::Sdf
                                        : EScreenTextureSampleMode::Coverage;
            appendSubTexture(pos,
                             scaledGlyphSize,
                             atlasTexture,
                             character.bColor ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : color,
                             character.uvRect,
                             static_cast<uint32_t>(sampleMode));
        }

        cursorX += character.advance.x * scale.x;
    }
}

void ScreenDrawList::drawRoundedRect(const glm::vec3& position,
                                     const glm::vec2& size,
                                     const glm::vec4& tint,
                                     float            cornerRadius)
{
    appendQuad(screenAffineFromPositionSize(position, size),
               findOrAddTexture(nullptr),
               static_cast<uint32_t>(EScreenTextureSampleMode::Coverage),
               {tint, tint, tint, tint}, {1.0f, 1.0f}, {0.0f, 0.0f},
               {cornerRadius, size.x, size.y});
}

void ScreenDrawList::makeRectFilledMultiColor(const glm::vec3&                position,
                                              const glm::vec2&                size,
                                              const std::array<glm::vec4, 4>& colors,
                                              Ptr<Texture>                    texture)
{
    const std::array<glm::vec4, 4> yaColors{colors[0], colors[1], colors[3], colors[2]};
    appendQuad(screenAffineFromPositionSize(position, size),
               findOrAddTexture(texture),
               static_cast<uint32_t>(EScreenTextureSampleMode::Coverage),
               yaColors, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
}

void ScreenDrawList::seal()
{
    closePendingCommand();
}

void ScreenDrawList::pushClipRect(const Rect2D& rect)
{
    Rect2D clipped = rect;
    if (!clipStack.empty()) {
        clipped = intersectClipRect(rect, clipStack.back());
    }

    const bool bClipChanged = clipStack.empty() ||
                              clipStack.back().pos != clipped.pos ||
                              clipStack.back().extent != clipped.extent;
    if (bClipChanged) {
        closePendingCommand();
    }
    clipStack.push_back(clipped);
}

void ScreenDrawList::popClipRect()
{
    if (clipStack.empty()) {
        return;
    }
    closePendingCommand();
    clipStack.pop_back();
}

} // namespace ya
