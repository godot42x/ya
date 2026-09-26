#include "Render2D/Render2DList.h"

#include "Render/Resources/FontManager.h"

#include <algorithm>

namespace ya
{

void Render2DList::beginBatch(ERender2dBatchKind kind)
{
    if (pendingKind == kind) {
        return;
    }
    closePendingCommand();
    pendingKind  = kind;
    pendingFirst = kind == ERender2dBatchKind::ScreenQuad ? static_cast<uint32_t>(screenVerts.size())
                 : kind == ERender2dBatchKind::WorldQuad  ? static_cast<uint32_t>(worldVerts.size())
                 : static_cast<uint32_t>(lineVerts.size());
    pendingCount = 0;
}

void Render2DList::closePendingCommand()
{
    if (pendingCount == 0) {
        pendingKind = ERender2dBatchKind::None;
        return;
    }
    // The clip snapshot is taken at close time: geometry emitted under one
    // clip shares one scissor, exactly like the immediate flusher's ordering.
    commands.push_back(Command{
        .kind        = pendingKind,
        .firstVertex = pendingFirst,
        .vertexCount = pendingCount,
        .bClipped    = !clipStack.empty(),
        .clip        = clipStack.empty() ? Rect2D{} : clipStack.back(),
    });
    if (pendingKind == ERender2dBatchKind::ScreenQuad) {
        ++screenCommandCount;
    }
    else if (pendingKind == ERender2dBatchKind::WorldQuad) {
        ++worldCommandCount;
    }
    pendingCount = 0;
    pendingKind  = ERender2dBatchKind::None;
}

FQuadRender::TextureRef Render2DList::findOrAddTexture(const Ptr<Texture>& texture,
                                                       FQuadRender::ETextureSampleMode mode)
{
    // The list-local table has no 16-entry limit: it only records which
    // texture each local slot refers to. The 16-slot pressure (and the
    // overflow-flush split) is a property of the pass's global binding table
    // and is handled by the record step. `mode` is not part of the table --
    // it rides on every vertex's ref.
    //
    // A null texture gets its OWN local slot exactly like a real one. If it
    // didn't, the first real texture would inherit slot 0 and a null-texture
    // draw would collide with it (a panel would end up sampling the font
    // atlas) -- the null entry is what makes local slot numbering match the
    // emit order of distinct textures.
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
    return FQuadRender::TextureRef{.slot = textureIdx, .mode = mode};
}

void Render2DList::appendScreenQuad(const glm::mat4&                transform,
                                    FQuadRender::TextureRef         textureRef,
                                    const std::array<glm::vec4, 4>& colorsYaOrder,
                                    const glm::vec2&                uvScale,
                                    const glm::vec2&                uvTranslation,
                                    const glm::vec3&                corner)
{
    screenVerts.resize(screenVerts.size() + 4);
    FQuadRender::EmitScreenQuad(screenVerts.data() + screenVerts.size() - 4,
                                transform, textureRef, colorsYaOrder, uvScale, uvTranslation, corner);
    pendingCount += 4;
}

void Render2DList::appendWorldQuad(const glm::vec3&        center,
                                   const glm::vec3&        direction,
                                   const glm::vec2&        size,
                                   FQuadRender::TextureRef textureRef,
                                   const glm::vec4&        tint,
                                   const glm::vec2&        uvScale)
{
    worldVerts.resize(worldVerts.size() + 4);
    FQuadRender::EmitWorldQuad(worldVerts.data() + worldVerts.size() - 4,
                               center, direction, size, textureRef, tint, uvScale);
    pendingCount += 4;
}

void Render2DList::appendLineSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    lineVerts.push_back(FLineRender::Vertex{.pos = from, .color = color});
    lineVerts.push_back(FLineRender::Vertex{.pos = to, .color = color});
    pendingCount += 2;
}

void Render2DList::makeSprite(const glm::vec3& position,
                              const glm::vec2& size,
                              Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale,
                              const glm::vec2& uvOffset,
                              bool             bOpaqueSample)
{
    beginBatch(ERender2dBatchKind::ScreenQuad);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    appendScreenQuad(model,
                     findOrAddTexture(texture,
                                      bOpaqueSample ? FQuadRender::ETextureSampleMode::Opaque
                                                    : FQuadRender::ETextureSampleMode::Coverage),
                     {tint, tint, tint, tint}, uvScale, uvOffset, {0.0f, 0.0f, 0.0f});
}

void Render2DList::makeSprite(const glm::mat4& transform,
                              Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale,
                              const glm::vec2& uvOffset,
                              bool             bOpaqueSample)
{
    beginBatch(ERender2dBatchKind::ScreenQuad);
    appendScreenQuad(transform,
                     findOrAddTexture(texture,
                                      bOpaqueSample ? FQuadRender::ETextureSampleMode::Opaque
                                                    : FQuadRender::ETextureSampleMode::Coverage),
                     {tint, tint, tint, tint}, uvScale, uvOffset, {0.0f, 0.0f, 0.0f});
}

void Render2DList::makeWorldSprite(const glm::vec3& worldCenter,
                                   const glm::vec3& worldDirection,
                                   const glm::vec2& worldSize,
                                   Ptr<Texture> texture,
                                   const glm::vec4& tint,
                                   const glm::vec2& uvScale)
{
    beginBatch(ERender2dBatchKind::WorldQuad);
    appendWorldQuad(worldCenter, worldDirection, worldSize,
                    findOrAddTexture(texture, FQuadRender::ETextureSampleMode::Coverage),
                    tint, uvScale);
}

void Render2DList::makeWorldLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    beginBatch(ERender2dBatchKind::Line);
    appendLineSegment(from, to, color);
}

void Render2DList::makeWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color)
{
    beginBatch(ERender2dBatchKind::Line);
    static constexpr std::array<glm::vec3, 8> corners = {{
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    }};
    static constexpr std::array<std::array<uint8_t, 2>, 12> edges = {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};

    for (const auto& edge : edges) {
        const glm::vec3 from = glm::vec3(model * glm::vec4(corners[edge[0]] * halfExtent, 1.0f));
        const glm::vec3 to   = glm::vec3(model * glm::vec4(corners[edge[1]] * halfExtent, 1.0f));
        appendLineSegment(from, to, color);
    }
}

void Render2DList::makeWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    beginBatch(ERender2dBatchKind::Line);
    static constexpr int   kSegmentCount = 24;
    static constexpr float kStep         = glm::two_pi<float>() / static_cast<float>(kSegmentCount);

    const auto addRing = [&](const glm::vec3& axisA, const glm::vec3& axisB)
    {
        for (int i = 0; i < kSegmentCount; ++i) {
            const float a0 = static_cast<float>(i) * kStep;
            const float a1 = static_cast<float>(i + 1) * kStep;
            appendLineSegment(center + radius * (axisA * glm::cos(a0) + axisB * glm::sin(a0)),
                              center + radius * (axisA * glm::cos(a1) + axisB * glm::sin(a1)),
                              color);
        }
    };

    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}); // XY
    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // XZ
    addRing({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}); // YZ
}

void Render2DList::appendSubTexture(const glm::vec3&                position,
                                    const glm::vec2&                size,
                                    const Ptr<Texture>&             texture,
                                    const glm::vec4&                tint,
                                    const glm::vec4&                uvRect,
                                    FQuadRender::ETextureSampleMode mode)
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    appendScreenQuad(model,
                     findOrAddTexture(texture, mode),
                     {tint, tint, tint, tint}, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y},
                     {0.0f, 0.0f, 0.0f});
}

void Render2DList::makeText(const std::string& text,
                            const glm::vec3&   position,
                            const glm::vec4&   color,
                            Font*              font,
                            const glm::vec2&   scale)
{
    beginBatch(ERender2dBatchKind::ScreenQuad);
    YA_CORE_ASSERT(font != nullptr, "Render2DList::makeText called with a null font");

    float cursorX = position.x;
    float cursorY = position.y;

    // Glyph capture is deferred to a safe frame point (requestGlyphs at
    // snapshot build + flushPendingGlyphs before recording -- Core Rule 6).
    // Missing glyphs resolve to '?' here and render correctly next frame.
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

        // Skip glyphs that haven't been captured yet (atlasSlot == ~0u): the
        // first frame after a new codepoint is requested renders the previous
        // frame's fallback instead of a degenerate quad that would sample a
        // single texel and look like a stray block.
        if (character.atlasSlot == ~0u || character.size.x <= 0 || character.size.y <= 0) {
            cursorX += character.advance.x * scale.x;
            continue;
        }

        float xpos = cursorX + static_cast<float>(character.bearing.x) * scale.x;
        float ypos = cursorY + static_cast<float>(font->ascent - character.bearing.y) * scale.y;
        // Pixel-snap glyph quads: subpixel positions cause uneven stroke
        // weight and a wavy baseline. Snap the DRAW position to device pixels;
        // the advance stays fractional so inter-glyph spacing keeps its
        // accumulated precision.
        xpos = std::round(xpos);
        ypos = std::round(ypos);
        glm::vec3 pos = glm::vec3(xpos, ypos, position.z);

        // Snap the glyph quad to an integer device-pixel footprint so an
        // integer quad gives Nearest sampling an exact texel->pixel map.
        const glm::vec2 scaledGlyphSize = glm::round(glm::vec2(character.size) * scale);

        // Glyphs live in the dynamic atlas of their face. Color glyphs (emoji)
        // draw with a WHITE tint so the bitmap's own colors show through.
        const auto atlasTexture = font->atlasTextureFor(character);
        if (atlasTexture) {
            const auto sampleMode = font->renderModeFor(character) == EFontRenderMode::SDF
                                        ? FQuadRender::ETextureSampleMode::Sdf
                                        : FQuadRender::ETextureSampleMode::Coverage;
            appendSubTexture(pos,
                             scaledGlyphSize,
                             atlasTexture,
                             character.bColor ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : color,
                             character.uvRect,
                             sampleMode);
        }

        cursorX += character.advance.x * scale.x;
    }
}

void Render2DList::drawRoundedRect(const glm::vec3& position,
                                   const glm::vec2& size,
                                   const glm::vec4& tint,
                                   float            cornerRadius)
{
    beginBatch(ERender2dBatchKind::ScreenQuad);
    // No texture: the white sprite fills the quad, the SDF round-rect branch
    // in the shader carves the corners from the quad's alpha. corner =
    // (radius, w, h) so the fragment shader can build the local-space
    // signed distance -- carried on the vertices, not in the table.
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
    appendScreenQuad(model,
                     findOrAddTexture(nullptr, FQuadRender::ETextureSampleMode::Coverage),
                     {tint, tint, tint, tint}, {1.0f, 1.0f}, {0.0f, 0.0f},
                     {cornerRadius, size.x, size.y});
}

void Render2DList::makeRectFilledMultiColor(const glm::vec3&                position,
                                            const glm::vec2&                size,
                                            const std::array<glm::vec4, 4>& colors,
                                            Ptr<Texture>                    texture)
{
    beginBatch(ERender2dBatchKind::ScreenQuad);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));

    // Public/ImGui order is TL, TR, BR, BL. Vertex buffer order is TL, TR, BL, BR.
    const std::array<glm::vec4, 4> yaColors{colors[0], colors[1], colors[3], colors[2]};
    appendScreenQuad(model,
                     findOrAddTexture(texture, FQuadRender::ETextureSampleMode::Coverage),
                     yaColors, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
}

void Render2DList::pushClipRect(const Rect2D& rect)
{
    // Intersect with the current clip so nested clips never exceed their parent.
    Rect2D clipped = rect;
    if (!clipStack.empty()) {
        clipped = intersectClipRect(rect, clipStack.back());
    }

    const bool bClipChanged = clipStack.empty() ||
                              clipStack.back().pos != clipped.pos ||
                              clipStack.back().extent != clipped.extent;
    if (bClipChanged) {
        // Flush whichever backend is pending with the CURRENT scissor BEFORE
        // switching clip; otherwise already-recorded geometry is either culled
        // by the incoming scissor or submitted after later draws.
        closePendingCommand();
    }
    clipStack.push_back(clipped);
}

void Render2DList::popClipRect()
{
    if (clipStack.empty()) {
        return;
    }
    // Content recorded inside the clip must close under the CURRENT (inner)
    // scissor BEFORE popping; otherwise it escapes the clip.
    closePendingCommand();
    clipStack.pop_back();
}

} // namespace ya
