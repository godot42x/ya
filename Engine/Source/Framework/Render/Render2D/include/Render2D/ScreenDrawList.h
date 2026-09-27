#pragma once

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

struct Texture;
struct Font;

/// Pure clip intersection used by the screen clip stack: `rect` clipped to
/// the current `parentClip`.
[[nodiscard]] inline Rect2D intersectClipRect(const Rect2D& rect, const Rect2D& parentClip)
{
    const glm::vec2 parentMax     = parentClip.pos + parentClip.extent;
    const glm::vec2 rectMax       = rect.pos + rect.extent;
    const glm::vec2 clippedPos    = glm::max(rect.pos, parentClip.pos);
    const glm::vec2 clippedExtent = glm::max(glm::vec2(0.0f), glm::min(rectMax, parentMax) - clippedPos);
    return Rect2D{.pos = clippedPos, .extent = clippedExtent};
}

/// Pixel-space 2D affine: `origin + xAxis * u + yAxis * v`. `z` is the quad's
/// depth in the screen ortho, which a 3×2 does not carry.
struct ScreenAffine
{
    glm::vec2 xAxis{1.0f, 0.0f};
    glm::vec2 yAxis{0.0f, 1.0f};
    glm::vec2 origin{0.0f, 0.0f};
    float     z = 0.0f;
};

enum class EScreenTextureSampleMode : uint8_t
{
    Coverage = 0,
    Sdf      = 1,
    Opaque   = 2,
};

/// Screen-space quad vertex for Sprite2DScreen.slang.
struct ScreenVertex
{
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 texCoord;
    uint32_t  textureSlot;
    uint32_t  sampleMode;
    glm::vec3 corner;
};

struct ScreenDrawFrameStats
{
    uint32_t screenFlushCount  = 0;
    uint32_t screenVertexCount = 0;
    uint32_t screenIndexCount  = 0;
};

/// CPU draw list in target pixels, top-left origin, Y-down. A command in this
/// list can be drawn without a camera. Building one touches no device.
/// Each command owns an index segment into `indices`. Those indices are
/// absolute positions in `vertices`, so a command can be any triangle list.
/// Quads still emit two triangles; they are not a special record path.
struct YA_RENDER_2D_API ScreenDrawList
{
    struct Command
    {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex  = 0;
        uint32_t indexCount  = 0;
        bool     bClipped    = false;
        Rect2D   clip{};
    };

    std::vector<Command>          commands;
    std::vector<ScreenVertex>     vertices;
    std::vector<uint32_t>         indices;
    std::vector<ya::Ptr<Texture>> textures;
    std::vector<Rect2D>           clipStack;

    void makeSprite(const glm::vec3& position,
                    const glm::vec2& size,
                    ya::Ptr<Texture> texture = nullptr,
                    const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                    const glm::vec2& uvScale = {1.0f, 1.0f},
                    const glm::vec2& uvOffset = {0.0f, 0.0f},
                    bool             bOpaqueSample = false);
    void makeSprite(const ScreenAffine& transform,
                    ya::Ptr<Texture>    texture = nullptr,
                    const glm::vec4&    tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                    const glm::vec2&    uvScale = {1.0f, 1.0f},
                    const glm::vec2&    uvOffset = {0.0f, 0.0f},
                    bool                bOpaqueSample = false);
    void makeText(const std::string& text,
                  const glm::vec3&   position,
                  const glm::vec4&   color,
                  Font*              font,
                  const glm::vec2&   scale = glm::vec2(1.0f));
    void drawRoundedRect(const glm::vec3& position,
                         const glm::vec2& size,
                         const glm::vec4& tint,
                         float            cornerRadius);
    void makeRectFilledMultiColor(const glm::vec3&                position,
                                  const glm::vec2&                size,
                                  const std::array<glm::vec4, 4>& colors,
                                  ya::Ptr<Texture>                texture = nullptr);

    /// Centered screen stroke. `feather` is the geometric AA fringe outside
    /// the core, in pixels; 0 keeps a hard edge. A zero-length segment becomes
    /// a square centered on `from`. Arc and bezier callers sample to points
    /// and then use the polyline.
    void strokeLine(const glm::vec2& from,
                    const glm::vec2& to,
                    const glm::vec4& color,
                    float            thickness,
                    float            feather = 0.0f,
                    float            z       = 0.0f);
    void strokePolyline(std::span<const glm::vec2> points,
                        bool                       bClosed,
                        const glm::vec4&           color,
                        float                      thickness,
                        float                      feather = 0.0f,
                        float                      z       = 0.0f);
    void strokeRect(const Rect2D&   rect,
                    const glm::vec4& color,
                    float            thickness,
                    float            feather = 0.0f,
                    float            z       = 0.0f);
    void fillConvexPoly(std::span<const glm::vec2> points,
                        const glm::vec4&           color,
                        float                      feather = 0.0f,
                        float                      z       = 0.0f);
    void strokeArc(const glm::vec2& center,
                   float            radius,
                   float            startRadians,
                   float            endRadians,
                   uint32_t         segments,
                   const glm::vec4& color,
                   float            thickness,
                   float            feather = 0.0f,
                   float            z       = 0.0f);
    void strokeBezierCubic(const glm::vec2& p0,
                           const glm::vec2& p1,
                           const glm::vec2& p2,
                           const glm::vec2& p3,
                           uint32_t         segments,
                           const glm::vec4& color,
                           float            thickness,
                           float            feather = 0.0f,
                           float            z       = 0.0f);

    void pushClipRect(const Rect2D& rect);
    void popClipRect();
    /// Close the open quad run. Record does this so the last run is not dropped.
    void seal();

    [[nodiscard]] uint32_t screenVertexCount() const { return static_cast<uint32_t>(vertices.size()); }

    [[nodiscard]] ScreenDrawFrameStats capturedStats() const
    {
        return ScreenDrawFrameStats{
            .screenFlushCount  = commandCount,
            .screenVertexCount = static_cast<uint32_t>(vertices.size()),
            .screenIndexCount  = static_cast<uint32_t>(indices.size()),
        };
    }

  private:
    void closePendingCommand();
    void ensurePending();
    [[nodiscard]] uint32_t findOrAddTexture(const Ptr<Texture>& texture);
    [[nodiscard]] uint32_t appendVertex(const ScreenVertex& vertex);
    void appendTriangle(uint32_t a, uint32_t b, uint32_t c);
    void appendQuadIndices(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    void appendQuad(const ScreenAffine&             transform,
                    uint32_t                        textureSlot,
                    uint32_t                        sampleMode,
                    const std::array<glm::vec4, 4>& colorsYaOrder,
                    const glm::vec2&                uvScale,
                    const glm::vec2&                uvTranslation,
                    const glm::vec3&                corner);
    void appendSubTexture(const glm::vec3&    position,
                          const glm::vec2&    size,
                          const Ptr<Texture>& texture,
                          const glm::vec4&    tint,
                          const glm::vec4&    uvRect,
                          uint32_t            sampleMode);

    std::unordered_map<const Texture*, uint32_t> texturePtr2Idx;
    uint32_t commandCount  = 0;
    bool     bPending      = false;
    uint32_t pendingFirst      = 0;
    uint32_t pendingCount      = 0;
    uint32_t pendingFirstIndex = 0;
    uint32_t pendingIndexCount = 0;
};

[[nodiscard]] inline ScreenAffine screenAffineFromPositionSize(const glm::vec3& position, const glm::vec2& size)
{
    return ScreenAffine{
        .xAxis  = {size.x, 0.0f},
        .yAxis  = {0.0f, size.y},
        .origin = {position.x, position.y},
        .z      = position.z,
    };
}

} // namespace ya
