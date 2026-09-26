#pragma once

#include "Render2D/LineRender.h"
#include "Render2D/QuadRender.h"

#include <glm/glm.hpp>

#include <array>
#include <unordered_map>
#include <vector>

namespace ya
{

struct Texture;
struct Font;

/// Pure clip intersection used by the clip stack: `rect` clipped to the
/// current `parentClip` (empty extent when disjoint).
[[nodiscard]] inline Rect2D intersectClipRect(const Rect2D& rect, const Rect2D& parentClip)
{
    const glm::vec2 parentMax    = parentClip.pos + parentClip.extent;
    const glm::vec2 rectMax      = rect.pos + rect.extent;
    const glm::vec2 clippedPos   = glm::max(rect.pos, parentClip.pos);
    const glm::vec2 clippedExtent = glm::max(glm::vec2(0.0f), glm::min(rectMax, parentMax) - clippedPos);
    return Rect2D{.pos = clippedPos, .extent = clippedExtent};
}

/// Which 2D backend a span of geometry belongs to. The list keeps at most one
/// kind pending so record order matches emit order across screen quads, world
/// quads, and debug lines.
enum class ERender2dBatchKind : uint8_t
{
    None = 0,
    ScreenQuad,
    WorldQuad,
    Line,
};

/// A recorded 2D draw list: the pure-CPU value `Render2D::recordRender2DList`
/// turns into GPU work.
///
/// Building one touches no command buffer, no pass slot and no device: clip
/// state, batch boundaries and the texture table are builder-local. Lists can
/// therefore be built anywhere (any thread, any time), inspected, and
/// unit-tested without a renderer.
///
/// Vertices are the same `FQuadRender::Vertex` / `FLineRender::Vertex` the GPU
/// consumes, with one translation: `textureRef` encodes a slot into this
/// list's local `textures` table, and the record step re-keys it into the
/// pass's global binding table while copying into the mapped vertex buffers.
struct YA_RENDER_2D_API Render2DList
{
    /// One contiguous batch of same-kind geometry under one clip rect.
    /// Boundaries mirror the immediate flusher's: kind change, clip change,
    /// end of build. `bClipped == false` means the full target extent.
    struct Command
    {
        ERender2dBatchKind kind;
        uint32_t           firstVertex = 0; // into the kind's vertex array
        uint32_t           vertexCount = 0; // 4*n (quads) / 2*n (segments)
        bool               bClipped    = false;
        Rect2D             clip{};
    };

    std::vector<Command>             commands;
    std::vector<FQuadRender::Vertex> screenVerts; // 4 per quad
    std::vector<FQuadRender::Vertex> worldVerts;  // 4 per quad
    std::vector<FLineRender::Vertex> lineVerts;   // 2 per segment
    /// List-local texture table (nullptr resolves to the white sprite at
    /// record time, exactly like the immediate path).
    std::vector<ya::Ptr<Texture>>    textures;
    std::vector<Rect2D>              clipStack;

    // ── Builder API: the shapes the static facade had, minus the device ──

    void makeSprite(const glm::vec3& position,
                    const glm::vec2& size,
                    ya::Ptr<Texture> texture = nullptr,
                    const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                    const glm::vec2& uvScale = {1.0f, 1.0f},
                    const glm::vec2& uvOffset = {0.0f, 0.0f},
                    bool             bOpaqueSample = false);
    void makeSprite(const glm::mat4& transform,
                    ya::Ptr<Texture> texture = nullptr,
                    const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                    const glm::vec2& uvScale = {1.0f, 1.0f},
                    const glm::vec2& uvOffset = {0.0f, 0.0f},
                    bool             bOpaqueSample = false);
    void makeWorldSprite(const glm::vec3& worldCenter,
                         const glm::vec3& worldDirection,
                         const glm::vec2& worldSize,
                         ya::Ptr<Texture> texture = nullptr,
                         const glm::vec4& tint    = {1.0f, 1.0f, 1.0f, 1.0f},
                         const glm::vec2& uvScale = {1.0f, 1.0f});
    void makeWorldLine(const glm::vec3& from,
                       const glm::vec3& to,
                       const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f});
    void makeWireBox(const glm::mat4& model,
                     const glm::vec3& halfExtent,
                     const glm::vec4& color = {0.2f, 0.9f, 0.3f, 1.0f});
    void makeWireSphere(const glm::vec3& center,
                        float            radius,
                        const glm::vec4& color = {0.3f, 0.6f, 1.0f, 1.0f});
    void makeText(const std::string& text,
                  const glm::vec3&   position,
                  const glm::vec4&   color,
                  Font*              font,
                  const glm::vec2&   scale = glm::vec2(1.0f));
    /// `cornerRadius` is in target px; the shader derives the SDF round-rect
    /// alpha from the quad size. No texture is sampled (white sprite fill).
    void drawRoundedRect(const glm::vec3& position,
                         const glm::vec2& size,
                         const glm::vec4& tint,
                         float            cornerRadius);
    /// Screen quad with a different color on each corner. `colors` is Y-down
    /// ImGui order: top-left, top-right, bottom-right, bottom-left.
    void makeRectFilledMultiColor(const glm::vec3&                position,
                                  const glm::vec2&                size,
                                  const std::array<glm::vec4, 4>& colors,
                                  ya::Ptr<Texture>                texture = nullptr);

    /// Push a clip rect (intersected with the current clip). Closing the
    /// pending batch first keeps already-emitted geometry under the current
    /// scissor, matching the immediate flusher's ordering.
    void pushClipRect(const Rect2D& rect);
    void popClipRect();

    [[nodiscard]] uint32_t screenVertexCount() const { return screenVerts.size(); }
    [[nodiscard]] uint32_t worldVertexCount() const { return worldVerts.size(); }
    [[nodiscard]] uint32_t lineVertexCount() const { return lineVerts.size(); }

    /// Flush-equivalent counters, incremented per closed command (the record
    /// step adds its own region splits). Capture consumers read these BEFORE
    /// appending overlay content, so the HUD never counts its own draws.
    [[nodiscard]] FQuadRender::FRender2dFrameStats capturedStats() const
    {
        return FQuadRender::FRender2dFrameStats{
            .screenFlushCount  = screenCommandCount,
            .worldFlushCount   = worldCommandCount,
            .screenVertexCount = static_cast<uint32_t>(screenVerts.size()),
            .screenIndexCount  = static_cast<uint32_t>(screenVerts.size()) * 6 / 4,
        };
    }

  private:
    void     beginBatch(ERender2dBatchKind kind);
    void     closePendingCommand();
    /// Resolve (or lazily add) a list-local texture slot. The returned ref
    /// encodes the LOCAL slot; the record step re-keys it into the pass's
    /// global binding table while copying. `mode` rides per-vertex, not per
    /// table entry.
    [[nodiscard]] FQuadRender::TextureRef findOrAddTexture(const Ptr<Texture>& texture,
                                                           FQuadRender::ETextureSampleMode mode);
    void     appendScreenQuad(const glm::mat4&                transform,
                              FQuadRender::TextureRef         textureRef,
                              const std::array<glm::vec4, 4>& colorsYaOrder,
                              const glm::vec2&                uvScale,
                              const glm::vec2&                uvTranslation,
                              const glm::vec3&                corner);
    void     appendWorldQuad(const glm::vec3&        center,
                             const glm::vec3&        direction,
                             const glm::vec2&        size,
                             FQuadRender::TextureRef textureRef,
                             const glm::vec4&        tint,
                             const glm::vec2&        uvScale);
    void     appendLineSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void     appendSubTexture(const glm::vec3&                position,
                              const glm::vec2&                size,
                              const Ptr<Texture>&             texture,
                              const glm::vec4&                tint,
                              const glm::vec4&                uvRect,
                              FQuadRender::ETextureSampleMode mode);

    /// Builder-local lookup for the texture table, and the pending batch the
    /// next command boundary closes. None of it survives the build.
    std::unordered_map<const Texture*, uint32_t> texturePtr2Idx;
    uint32_t screenCommandCount = 0;
    uint32_t worldCommandCount  = 0;
    ERender2dBatchKind pendingKind  = ERender2dBatchKind::None;
    uint32_t           pendingFirst = 0;
    uint32_t           pendingCount = 0;
};

} // namespace ya
