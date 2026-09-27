#include "Render2D/ScreenDrawList.h"

#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

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
        .firstIndex  = pendingFirstIndex,
        .indexCount  = pendingIndexCount,
        .bClipped    = !clipStack.empty(),
        .clip        = clipStack.empty() ? Rect2D{} : clipStack.back(),
    });
    ++commandCount;
    pendingCount      = 0;
    pendingIndexCount = 0;
    bPending          = false;
}

void ScreenDrawList::ensurePending()
{
    if (bPending) {
        return;
    }
    bPending           = true;
    pendingFirst       = static_cast<uint32_t>(vertices.size());
    pendingCount       = 0;
    pendingFirstIndex  = static_cast<uint32_t>(indices.size());
    pendingIndexCount  = 0;
}

uint32_t ScreenDrawList::appendVertex(const ScreenVertex& vertex)
{
    ensurePending();
    vertices.push_back(vertex);
    ++pendingCount;
    return static_cast<uint32_t>(vertices.size() - 1);
}

void ScreenDrawList::appendTriangle(uint32_t a, uint32_t b, uint32_t c)
{
    ensurePending();
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
    pendingIndexCount += 3;
}

void ScreenDrawList::appendQuadIndices(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    appendTriangle(a, b, d);
    appendTriangle(a, d, c);
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
    ensurePending();
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.resize(vertices.size() + 4);
    emitQuad(vertices.data() + vertices.size() - 4,
             transform, textureSlot, sampleMode,
             colorsYaOrder, uvScale, uvTranslation, corner);
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 3);
    indices.push_back(base + 0);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
    pendingCount += 4;
    pendingIndexCount += 6;
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

namespace
{

constexpr float kStrokeDegenerateLength = 1e-4f;

ScreenVertex coverageVertex(const glm::vec2& xy, float z, const glm::vec4& color, uint32_t textureSlot)
{
    return ScreenVertex{
        .pos         = {xy.x, xy.y, z},
        .color       = color,
        .texCoord    = {0.0f, 0.0f},
        .textureSlot = textureSlot,
        .sampleMode  = static_cast<uint32_t>(EScreenTextureSampleMode::Coverage),
        .corner      = {0.0f, 0.0f, 0.0f},
    };
}

} // namespace

void ScreenDrawList::strokeLine(const glm::vec2& from,
                                const glm::vec2& to,
                                const glm::vec4& color,
                                float            thickness,
                                float            feather,
                                float            z)
{
    thickness = std::max(thickness, 0.0f);
    feather   = std::max(feather, 0.0f);
    if (thickness <= 0.0f && feather <= 0.0f) {
        return;
    }

    const glm::vec2 delta = to - from;
    const float     len   = glm::length(delta);
    const float     half  = thickness * 0.5f;
    const uint32_t  slot  = findOrAddTexture(nullptr);

    if (feather <= 0.0f) {
        if (len <= kStrokeDegenerateLength) {
            const glm::vec2 extent{thickness, thickness};
            makeSprite(glm::vec3(from - extent * 0.5f, z), extent, nullptr, color);
            return;
        }
        const glm::vec2 dir = delta / len;
        const glm::vec2 nrm{-dir.y, dir.x};
        makeSprite(ScreenAffine{
                       .xAxis  = dir * len,
                       .yAxis  = nrm * thickness,
                       .origin = from - nrm * half,
                       .z      = z,
                   },
                   nullptr,
                   color);
        return;
    }

    auto push = [&](const glm::vec2& xy, float alpha) {
        glm::vec4 tint = color;
        tint.a *= alpha;
        return appendVertex(coverageVertex(xy, z, tint, slot));
    };

    if (len <= kStrokeDegenerateLength) {
        const float outer = half + feather;
        const uint32_t inner[4] = {
            push(from + glm::vec2(-half, -half), 1.0f),
            push(from + glm::vec2(half, -half), 1.0f),
            push(from + glm::vec2(-half, half), 1.0f),
            push(from + glm::vec2(half, half), 1.0f),
        };
        const uint32_t outerIds[4] = {
            push(from + glm::vec2(-outer, -outer), 0.0f),
            push(from + glm::vec2(outer, -outer), 0.0f),
            push(from + glm::vec2(-outer, outer), 0.0f),
            push(from + glm::vec2(outer, outer), 0.0f),
        };
        appendQuadIndices(inner[0], inner[1], inner[2], inner[3]);
        appendQuadIndices(outerIds[0], outerIds[1], inner[0], inner[1]);
        appendQuadIndices(outerIds[1], outerIds[3], inner[1], inner[3]);
        appendQuadIndices(outerIds[3], outerIds[2], inner[3], inner[2]);
        appendQuadIndices(outerIds[2], outerIds[0], inner[2], inner[0]);
        return;
    }

    const glm::vec2 dir = delta / len;
    const glm::vec2 nrm{-dir.y, dir.x};
    const float     offsets[4] = {-half - feather, -half, half, half + feather};
    const float     alphas[4]  = {0.0f, 1.0f, 1.0f, 0.0f};
    uint32_t        ids[4][2]{};
    for (int row = 0; row < 4; ++row) {
        const glm::vec2 shift = nrm * offsets[row];
        ids[row][0]           = push(from + shift, alphas[row]);
        ids[row][1]           = push(to + shift, alphas[row]);
    }
    for (int row = 0; row < 3; ++row) {
        appendQuadIndices(ids[row][0], ids[row][1], ids[row + 1][0], ids[row + 1][1]);
    }
}

void ScreenDrawList::strokePolyline(std::span<const glm::vec2> points,
                                    bool                       bClosed,
                                    const glm::vec4&           color,
                                    float                      thickness,
                                    float                      feather,
                                    float                      z)
{
    if (points.empty()) {
        return;
    }
    if (points.size() == 1) {
        strokeLine(points[0], points[0], color, thickness, feather, z);
        return;
    }

    const size_t count    = points.size();
    const size_t segments = bClosed ? count : count - 1;
    uint32_t     drawn    = 0;
    for (size_t i = 0; i < segments; ++i) {
        const glm::vec2& a = points[i];
        const glm::vec2& b = points[(i + 1) % count];
        if (glm::length(b - a) <= kStrokeDegenerateLength) {
            continue;
        }
        strokeLine(a, b, color, thickness, feather, z);
        ++drawn;
    }
    if (drawn == 0) {
        strokeLine(points[0], points[0], color, thickness, feather, z);
    }
}

void ScreenDrawList::strokeRect(const Rect2D&   rect,
                                const glm::vec4& color,
                                float            thickness,
                                float            feather,
                                float            z)
{
    const glm::vec2 corners[4] = {
        rect.pos,
        rect.pos + glm::vec2(rect.extent.x, 0.0f),
        rect.pos + rect.extent,
        rect.pos + glm::vec2(0.0f, rect.extent.y),
    };
    strokePolyline(corners, true, color, thickness, feather, z);
}

void ScreenDrawList::fillConvexPoly(std::span<const glm::vec2> points,
                                    const glm::vec4&           color,
                                    float                      feather,
                                    float                      z)
{
    feather = std::max(feather, 0.0f);
    if (points.size() < 3) {
        return;
    }

    const uint32_t slot = findOrAddTexture(nullptr);
    std::vector<uint32_t> ids;
    ids.reserve(points.size());
    glm::vec2 centroid{0.0f};
    for (const glm::vec2& point : points) {
        centroid += point;
        ids.push_back(appendVertex(coverageVertex(point, z, color, slot)));
    }
    centroid /= static_cast<float>(points.size());
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        appendTriangle(ids[0], ids[i], ids[i + 1]);
    }
    if (feather <= 0.0f) {
        return;
    }

    glm::vec4 clear = color;
    clear.a         = 0.0f;
    const size_t count = points.size();
    for (size_t i = 0; i < count; ++i) {
        const glm::vec2& a     = points[i];
        const glm::vec2& b     = points[(i + 1) % count];
        const glm::vec2  edge  = b - a;
        const float      len   = glm::length(edge);
        if (len <= kStrokeDegenerateLength) {
            continue;
        }
        glm::vec2 normal{-edge.y / len, edge.x / len};
        const glm::vec2 mid = (a + b) * 0.5f;
        if (glm::dot(normal, mid - centroid) < 0.0f) {
            normal = -normal;
        }
        const glm::vec2 shift = normal * feather;
        const uint32_t  outerA = appendVertex(coverageVertex(a + shift, z, clear, slot));
        const uint32_t  outerB = appendVertex(coverageVertex(b + shift, z, clear, slot));
        appendQuadIndices(ids[i], ids[(i + 1) % count], outerA, outerB);
    }
}

void ScreenDrawList::strokeArc(const glm::vec2& center,
                               float            radius,
                               float            startRadians,
                               float            endRadians,
                               uint32_t         segments,
                               const glm::vec4& color,
                               float            thickness,
                               float            feather,
                               float            z)
{
    if (segments == 0) {
        return;
    }
    const bool bClosed = std::abs(endRadians - startRadians) >= std::numbers::pi_v<float> * 2.0f - 1e-3f;
    const uint32_t pointCount = bClosed ? segments : segments + 1;
    std::vector<glm::vec2> points;
    points.reserve(pointCount);
    for (uint32_t i = 0; i < pointCount; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float t = startRadians + (endRadians - startRadians) * u;
        points.push_back(center + glm::vec2(std::cos(t), std::sin(t)) * radius);
    }
    strokePolyline(points, bClosed, color, thickness, feather, z);
}

void ScreenDrawList::strokeBezierCubic(const glm::vec2& p0,
                                       const glm::vec2& p1,
                                       const glm::vec2& p2,
                                       const glm::vec2& p3,
                                       uint32_t         segments,
                                       const glm::vec4& color,
                                       float            thickness,
                                       float            feather,
                                       float            z)
{
    if (segments == 0) {
        return;
    }
    std::vector<glm::vec2> points;
    points.reserve(static_cast<size_t>(segments) + 1);
    for (uint32_t i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        points.push_back(u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3);
    }
    strokePolyline(points, false, color, thickness, feather, z);
}

} // namespace ya
