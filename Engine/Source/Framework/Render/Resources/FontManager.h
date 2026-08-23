#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/ResourceRegistry.h"
#include "DynamicFontAtlas.h"
#include "IFontRasterizer.h"
#include "RHI/Core/Texture.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ya
{

struct IRender;

inline constexpr const char* DEFAULT_RUNTIME_FONT_NAME = "RuntimeDefault";
inline constexpr uint32_t    DEFAULT_RUNTIME_FONT_SIZE = 48;

namespace utf8
{
inline bool decodeNext(std::string_view text, size_t& offset, uint32_t& codePoint)
{
    if (offset >= text.size()) {
        return false;
    }

    const unsigned char lead = static_cast<unsigned char>(text[offset++]);
    if (lead < 0x80) {
        codePoint = lead;
        return true;
    }

    auto readContinuation = [&](uint32_t& value) -> bool {
        if (offset >= text.size()) {
            return false;
        }
        const unsigned char ch = static_cast<unsigned char>(text[offset]);
        if ((ch & 0xC0) != 0x80) {
            return false;
        }
        ++offset;
        value = (value << 6) | (ch & 0x3F);
        return true;
    };

    if ((lead & 0xE0) == 0xC0) {
        uint32_t value = lead & 0x1F;
        if (!readContinuation(value)) {
            codePoint = 0xFFFD;
            return true;
        }
        codePoint = value;
        return true;
    }
    if ((lead & 0xF0) == 0xE0) {
        uint32_t value = lead & 0x0F;
        if (!readContinuation(value) || !readContinuation(value)) {
            codePoint = 0xFFFD;
            return true;
        }
        codePoint = value;
        return true;
    }
    if ((lead & 0xF8) == 0xF0) {
        uint32_t value = lead & 0x07;
        if (!readContinuation(value) || !readContinuation(value) || !readContinuation(value)) {
            codePoint = 0xFFFD;
            return true;
        }
        codePoint = value;
        return true;
    }

    codePoint = 0xFFFD;
    return true;
}

inline std::vector<uint32_t> decode(std::string_view text)
{
    std::vector<uint32_t> codePoints;
    codePoints.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        uint32_t codePoint = 0;
        if (!decodeNext(text, offset, codePoint)) {
            break;
        }
        codePoints.push_back(codePoint);
    }
    return codePoints;
}
} // namespace utf8

struct GlyphDesc
{
};


struct Character
{
    glm::vec4                uvRect;    // UV rect: (offsetU, offsetV, scaleU, scaleV) for drawSubTexture
    glm::ivec2               size;      // Size of glyph in pixels
    glm::ivec2               bearing;   // Offset from baseline to left/top of glyph
    glm::vec2                advance;   // Horizontal offset to advance to next glyph
    uint32_t                 atlasSlot = ~0u;  // DynamicFontAtlas slot (UVs re-read after repack)
    uint16_t                 atlasIndex = 0;  // 0 = primary atlas, 1.. = fallback face atlas (font stack)
    uint32_t                 designSize = 0;  // rasterization size this glyph was captured at (0 = primary base size)
    bool                     bColor     = false; // Color glyph (emoji): draw with white tint, no text-color modulation
    bool                     bInAtlas   = true;  // All glyphs live in the dynamic atlas (standalone path removed)
};

class IFontRasterizer;

/// One fallback face in a font stack (plan Phase 3): CJK / emoji / ... Each
/// entry owns its own rasterizer + atlas (color emoji needs a separate RGBA
/// atlas; CJK uses a smaller SDF base to bound memory). Metrics stay
/// primary-face-driven; fallback glyphs contribute their own advance/bearing.
struct FFontStackEntry
{
    std::string                      fontPath;
    EFontRenderMode                  renderMode = EFontRenderMode::Bitmap;
    uint32_t                         baseSize   = 64;  // rasterization size for this face
    std::shared_ptr<DynamicFontAtlas> atlas      = nullptr;
    std::shared_ptr<IFontRasterizer>  rasterizer = nullptr;
    std::shared_ptr<Texture>          atlasTexture = nullptr;
};

/**
 * @brief Font - Font atlas and glyph data
 */
struct Font
{
    std::unordered_map<uint32_t, Character> characters;
    /// Codepoints the whole font stack FAILED to rasterize (e.g. emoji
    /// variation selectors): requesting them again every frame would re-run
    /// capture + atlas upload forever (per-frame texture churn). They render
    /// as the '?' fallback.
    std::unordered_set<uint32_t>            missing;
    EFontRenderMode                         renderMode = EFontRenderMode::Bitmap;
    float                                   fontSize   = 0;
    float                                   lineHeight = 0;         // Line height (ascender - descender + line gap)
    float                                   ascent     = 0;         // Distance from baseline to top of tallest glyph
    float                                   descent    = 0;         // Distance from baseline to bottom of lowest glyph
    std::string                             fontPath;               // Path to font file (primary face)
    std::shared_ptr<Texture>                atlasTexture = nullptr; // Primary atlas texture (optional)
    std::shared_ptr<DynamicFontAtlas>       atlas        = nullptr; // Primary growable atlas (host-loaded fonts)
    std::shared_ptr<IFontRasterizer>        rasterizer   = nullptr; // Primary glyph flavor rasterizer
    std::vector<FFontStackEntry>            fallbacks;              // Ordered fallback chain (CJK/emoji/...)
    /// Scaled view over a base font: shares the atlas texture; metrics are
    /// pre-scaled to fontSize. baseFont is null for the base font itself.
    std::shared_ptr<Font> baseFont;
    float                 scale = 1.0f;

    /// Atlas texture for a character (primary or fallback face). Reads the
    /// LIVE atlas handle so repack/upload updates propagate to scaled views
    /// that share the fallback chain — the cached atlasTexture fields on a
    /// scaled view are copies from view creation and go stale after any
    /// upload (page switches capture new glyphs -> upload -> stale pointer
    /// -> whole-text garbage).
    [[nodiscard]] std::shared_ptr<Texture> atlasTextureFor(const Character& ch) const
    {
        if (ch.atlasIndex == 0 || ch.atlasIndex > fallbacks.size()) {
            return atlas ? atlas->texture() : atlasTexture;
        }
        const auto& fbAtlas = fallbacks[ch.atlasIndex - 1].atlas;
        return fbAtlas ? fbAtlas->texture() : fallbacks[ch.atlasIndex - 1].atlasTexture;
    }

    [[nodiscard]] bool isView() const { return baseFont != nullptr; }

    bool hasCharacter(uint32_t codePoint) const { return characters.contains(codePoint); }
    bool hasCharacter(char asciiCode) const { return hasCharacter(static_cast<uint32_t>(static_cast<uint8_t>(asciiCode))); }
    bool hasCharacter(wchar_t wideChar) const { return hasCharacter(static_cast<uint32_t>(wideChar)); }

    float            getFontSize() const { return fontSize; }
    const Character &getCharacter(uint32_t codePoint) const
    {
        static Character defaultChar{};
        auto             it = characters.find(codePoint);
        if (it != characters.end()) {
            return it->second;
        }

        it = characters.find(static_cast<uint32_t>('?'));
        if (it != characters.end()) {
            return it->second;
        }
        return defaultChar;
    }

    const Character &getCharacter(char c) const { return getCharacter(static_cast<uint32_t>(static_cast<uint8_t>(c))); }

    float measureText(const std::string &text) const
    {
        float width     = 0.0f;
        float maxWidth  = 0.0f;
        float tabWidth  = getCharacter(' ').advance.x * 4.0f;
        auto  codePoints = utf8::decode(text);
        for (uint32_t codePoint : codePoints) {
            if (codePoint == '\r') {
                continue;
            }
            if (codePoint == '\n') {
                maxWidth = std::max(maxWidth, width);
                width = 0.0f;
                continue;
            }
            if (codePoint == '\t') {
                width += tabWidth;
                continue;
            }
            width += getCharacter(codePoint).advance.x;
        }
        return std::max(maxWidth, width);
    }
};

struct YA_RENDER_RESOURCES_API FontManager : public IResourceCache
{

    /// Injected sink receiving freshly created font atlas textures. The GUI
    /// framework stays decoupled from the game-side asset manager: hosts
    /// (engine runtime / editor) register a sink that forwards the texture to
    /// their own registry (e.g. AssetManager::registerTexture). Pure GUI hosts
    /// leave it unset and the texture simply stays owned by the Font.
    using FontAtlasTextureSink = std::function<void(const FName& fontName, uint32_t fontSize, const std::shared_ptr<Texture>& atlasTexture)>;

  private:
    // Key: "fontName:fontSize" -> Font
    std::unordered_map<std::string, stdptr<Font>> _fontCache;
    // Base font per name (single atlas, metrics at the rasterization size).
    std::unordered_map<FName, stdptr<Font>>       _baseFontCache;
    FontAtlasTextureSink                           _fontAtlasTextureSink;
    // Missing glyphs awaiting safe-point capture (Core Rule 6): base-font ptr
    // -> codepoints. Flushed by flushPendingGlyphs at a frame boundary.
    std::unordered_map<Font*, std::unordered_set<uint32_t>> _pendingGlyphs;
    bool _bNewGlyphsCaptured = false;

  public:
    static FontManager *get();

    /// Install the host-provided font atlas texture sink (called once at host
    /// startup; passing nullptr clears it).
    static void setFontAtlasTextureSink(FontAtlasTextureSink sink);

    // IResourceCache interface
    void  clearCache() override;
    FName getCacheName() const override { return "FontManager"; }

    /**
     * @brief Load a font with specific size
     * @param fontPath Path to font file
     * @param fontName Unique font identifier
     * @param fontSize Font size in pixels
     * @return Shared pointer to loaded font, or nullptr on failure
     */
    /// Load a font rasterized in `renderMode`. Bitmap = grayscale coverage
    /// (legacy); SDF = FreeType distance field (scale-free, crisp at any
    /// size — the GUI default once enabled by the host).
    std::shared_ptr<Font> loadFont(IRender& render, const std::string &fontPath, const FName &fontName, uint32_t fontSize,
                                   EFontRenderMode renderMode = EFontRenderMode::Bitmap);

    std::shared_ptr<Font> getFont(const FName &fontName, uint32_t fontSize);

    /// Append a fallback face to the font stack (plan Phase 3): glyphs the
    /// primary face cannot render resolve through the fallbacks in order
    /// (e.g. CJK via a system font, emoji via a color font). Each fallback
    /// gets its own rasterizer + atlas. Must be called after loadFont.
    bool addFontFallback(IRender& render, const FName& fontName, const std::string& fontPath,
                         EFontRenderMode renderMode, uint32_t baseSize);

    /// Shared font-stack candidates (plan Phase 3): engine-bundled or
    /// platform CJK fonts, in preference order. Used by GUI + game hosts.
    static std::vector<std::string> findCjkFontCandidates();
    /// Bundled color-emoji font (seguiemj.ttf) when present.
    static std::string findEmojiFontPath();

    /// Pre-register a font under `name:size` so getFont() returns it without
    /// loading (rasterizer + GPU not needed). Hosts that pre-build glyph data
    /// and layout tests injecting synthetic fonts use this; getFont already
    /// serves cached entries first, so production loading is unchanged.
    void registerFont(const FName &fontName, uint32_t fontSize, std::shared_ptr<Font> font);

    void unloadFont(const FName &fontName, uint32_t fontSize);

    /**
     * @brief Get or load font with size adapted to window height
     * @param fontPath Path to font file
     * @param fontName Font identifier
     * @param baseSize Base font size (at reference height, e.g., 1080p)
     * @param windowHeight Current window height in pixels
     * @param referenceHeight Reference height (default: 1080)
     * @return Adapted font
     */
    std::shared_ptr<Font> getAdaptiveFont(IRender&            render,
                                          const std::string &fontPath,
                                          const FName       &fontName,
                                          uint32_t           baseSize,
                                          uint32_t           windowHeight,
                                          uint32_t           referenceHeight = 1080);

    void ensureGlyphs(IRender& render, Font& font, std::string_view text);

    /// Register missing glyphs of `text` for lazy capture (no GPU work). The
    /// host calls flushPendingGlyphs at a safe frame point (after snapshot
    /// build, before command recording — Core Rule 6). Missing glyphs render
    /// as '?' until the next flush (standard 1-frame latency).
    /// Returns true when NEW glyphs were registered (i.e. some codepoint was
    /// missing): the caller should invalidate layout — text measured against
    /// the '?' fallback is stale until the next flush + re-measure.
    bool requestGlyphs(Font& font, std::string_view text);
    /// Rasterize + add all pending glyphs into their font's dynamic atlas
    /// (grow/repack as needed). Safe-point only. Sets an internal flag when
    /// glyphs were actually captured; consumeNewGlyphCapture() reports it.
    void flushPendingGlyphs(IRender& render);
    /// True when the last flush captured new glyphs (and clears the flag):
    /// text items measured/rendered against the '?' fallback are stale — the
    /// host should invalidate layout + paint so they re-measure/re-paint.
    bool consumeNewGlyphCapture();

    // TODO: optimize key generation
    static std::string makeCacheKey(const FName &fontName, uint32_t fontSize)
    {
        return std::format("{}:{}", fontName.toString(), fontSize);
    }
};

} // namespace ya
