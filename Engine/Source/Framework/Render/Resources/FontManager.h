#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Core/ResourceRegistry.h"
#include "DynamicFontAtlas.h"
#include "FontAtlasBank.h"
#include "IFontRasterizer.h"
#include "RHI/Core/Texture.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ya
{

/// Size threshold (px) for the bitmap / SDF split (font-framework plan §1).
/// Glyphs rendered at or below this size use FreeType's hinted grayscale
/// bitmap (crisp at small sizes, no SDF under-sampling bite-out); larger
/// glyphs use the distance field (scale-free, rotation-stable). 20px is the
/// Pixel-size ceiling for the hinted grayscale bitmap flavor. Below/at it a
/// single-channel 8-bit SDF cannot preserve thin strokes (the internal solid
/// band collapses under linear sampling and bites strokes out). GUI text is
/// drawn at a fixed logical size and barely scaled, so bitmap stays crisp here.
/// Only genuinely large glyphs (headings / scalable art) benefit from SDF's
/// scale freedom, and at those sizes the 64px SDF base has enough distance-field
/// headroom to not bite. 48px keeps the common 13-40px UI text on the bitmap
/// path, which is also what FreeType hinting is tuned for.
constexpr uint32_t kBitmapMaxSize = 48;

/// Rasterization base size for SDF glyphs. SDF needs headroom around each
/// stroke to encode a usable distance field; rasterizing at the target size
/// (e.g. 13px) leaves no band and collapses thin strokes. 64px base + a
/// scaled view keeps the field well-sampled while staying memory-bounded.
constexpr uint32_t kSdfBaseSize = 64;

/// Picks the rasterization flavor for a requested pixel size. Pure function so
/// the split stays consistent across loadFont / getAdaptiveFont / fallback.
inline EFontRenderMode chooseModeForSize(uint32_t sizePx)
{
    return sizePx <= kBitmapMaxSize ? EFontRenderMode::Bitmap : EFontRenderMode::SDF;
}

[[nodiscard]] inline const char* fontRenderModeName(EFontRenderMode mode)
{
    switch (mode) {
    case EFontRenderMode::Bitmap:
        return "Bitmap";
    case EFontRenderMode::MSDF:
        return "MSDF";
    case EFontRenderMode::SDF:
        return "SDF";
    case EFontRenderMode::Color:
        return "Color";
    }
    return "?";
}

struct IRender;

inline constexpr const char* DEFAULT_RUNTIME_FONT_NAME = "RuntimeDefault";
inline constexpr uint32_t    DEFAULT_RUNTIME_FONT_SIZE = 48;

namespace utf8
{
// UTF-8 byte layout constants (RFC 3629).
inline constexpr uint32_t UTF8_1BYTE_MAX     = 0x80;   // Lead byte < 0x80 decodes to a single ASCII code point.
inline constexpr uint32_t UTF8_CONTINUATION_MASK  = 0xC0; // Top 2 bits of a continuation byte.
inline constexpr uint32_t UTF8_CONTINUATION_MARK = 0x80; // Continuation bytes are 10xxxxxx (0x80..0xBF).
inline constexpr uint32_t UTF8_CONTINUATION_DATA  = 0x3F; // Low 6 bits carry the payload of a continuation byte.
inline constexpr uint32_t UTF8_2BYTE_MASK    = 0xE0;  // Lead mask for 2-byte sequences.
inline constexpr uint32_t UTF8_2BYTE_MARK    = 0xC0;  // 110xxxxx lead.
inline constexpr uint32_t UTF8_2BYTE_DATA    = 0x1F;  // Low 5 bits of a 2-byte lead.
inline constexpr uint32_t UTF8_3BYTE_MASK    = 0xF0;  // Lead mask for 3-byte sequences.
inline constexpr uint32_t UTF8_3BYTE_MARK    = 0xE0;  // 1110xxxx lead.
inline constexpr uint32_t UTF8_3BYTE_DATA    = 0x0F;  // Low 4 bits of a 3-byte lead.
inline constexpr uint32_t UTF8_4BYTE_MASK    = 0xF8;  // Lead mask for 4-byte sequences.
inline constexpr uint32_t UTF8_4BYTE_MARK    = 0xF0;  // 11110xxx lead.
inline constexpr uint32_t UTF8_4BYTE_DATA    = 0x07;  // Low 3 bits of a 4-byte lead.
inline constexpr uint32_t UNICODE_REPLACEMENT = 0xFFFD; // U+FFFD replacement character for invalid sequences.

inline bool decodeNext(std::string_view text, size_t& offset, uint32_t& codePoint)
{
    if (offset >= text.size()) {
        return false;
    }

    const unsigned char lead = static_cast<unsigned char>(text[offset++]);
    if (lead < UTF8_1BYTE_MAX) {
        codePoint = lead;
        return true;
    }

    auto readContinuation = [&](uint32_t& value) -> bool {
        if (offset >= text.size()) {
            return false;
        }
        const unsigned char ch = static_cast<unsigned char>(text[offset]);
        if ((ch & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_MARK) {
            return false;
        }
        ++offset;
        value = (value << 6) | (ch & UTF8_CONTINUATION_DATA);
        return true;
    };

    if ((lead & UTF8_2BYTE_MASK) == UTF8_2BYTE_MARK) {
        uint32_t value = lead & UTF8_2BYTE_DATA;
        if (!readContinuation(value)) {
            codePoint = UNICODE_REPLACEMENT;
            return true;
        }
        codePoint = value;
        return true;
    }
    if ((lead & UTF8_3BYTE_MASK) == UTF8_3BYTE_MARK) {
        uint32_t value = lead & UTF8_3BYTE_DATA;
        if (!readContinuation(value) || !readContinuation(value)) {
            codePoint = UNICODE_REPLACEMENT;
            return true;
        }
        codePoint = value;
        return true;
    }
    if ((lead & UTF8_4BYTE_MASK) == UTF8_4BYTE_MARK) {
        uint32_t value = lead & UTF8_4BYTE_DATA;
        if (!readContinuation(value) || !readContinuation(value) || !readContinuation(value)) {
            codePoint = UNICODE_REPLACEMENT;
            return true;
        }
        codePoint = value;
        return true;
    }

    codePoint = UNICODE_REPLACEMENT;
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

inline bool isIgnorableFormatCodePoint(uint32_t codePoint)
{
    switch (codePoint) {
    case 0x200B:  // ZERO WIDTH SPACE 零宽空格
    case 0x200C:  // ZERO WIDTH NON-JOINER 零宽非连字
    case 0x200D:  // ZERO WIDTH JOINER 零宽连字（emoji 组合用，如 ‍👨‍👩‍👧）
    case 0x2060:  // WORD JOINER 词连接符
    case 0xFE0E:  // VARIATION SELECTOR-15 变体选择符（强制文本呈现）
    case 0xFE0F:  // VARIATION SELECTOR-16 变体选择符（强制 emoji 呈现）
        return true;
    default:
        return false;
    }
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
    std::shared_ptr<FontAtlasBank>   atlas      = nullptr; // paged atlas (one per fallback face)
    std::shared_ptr<IFontRasterizer>  rasterizer = nullptr;
    std::shared_ptr<Texture>          atlasTexture = nullptr; // deprecated mirror; use bank
};

/**
 * @brief Font - Font atlas and glyph data
 */
struct Font
{
    std::unordered_map<uint32_t, Character> characters;
    EFontRenderMode                         renderMode = EFontRenderMode::Bitmap;
    float                                   fontSize   = 0;
    float                                   lineHeight = 0;         // Line height (ascender - descender + line gap)
    float                                   ascent     = 0;         // Distance from baseline to top of tallest glyph
    float                                   descent    = 0;         // Distance from baseline to bottom of lowest glyph
    std::string                             fontPath;               // Path to font file (primary face)
    std::shared_ptr<Texture>                atlasTexture = nullptr; // deprecated mirror; use atlas (bank)
    std::shared_ptr<FontAtlasBank>          atlas        = nullptr; // Primary paged atlas (host-loaded fonts)
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
            return atlas ? atlas->textureForSlot(ch.atlasSlot) : atlasTexture;
        }
        const auto& fbAtlas = fallbacks[ch.atlasIndex - 1].atlas;
        return fbAtlas ? fbAtlas->textureForSlot(ch.atlasSlot) : fallbacks[ch.atlasIndex - 1].atlasTexture;
    }

    [[nodiscard]] EFontRenderMode renderModeFor(const Character& ch) const
    {
        if (ch.atlasIndex == 0 || ch.atlasIndex > fallbacks.size()) {
            return renderMode;
        }
        return fallbacks[ch.atlasIndex - 1].renderMode;
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
            if (utf8::isIgnorableFormatCodePoint(codePoint)) {
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
    // Key: "fontName:fontSize" -> Font (base or scaled view).
    std::unordered_map<std::string, stdptr<Font>> _fontCache;
    // Base font per (name, size). A single font family can have multiple
    // rasterization bases (e.g. SDF at 16/32/64 px) so getFont() can pick the
    // closest base instead of scaling one huge atlas down to tiny sizes.
    std::unordered_map<std::string, stdptr<Font>> _baseFontCache;
    // Fast lookup of the available base sizes registered for each font name.
    std::unordered_map<FName, std::vector<uint32_t>> _baseSizes;
    FontAtlasTextureSink                           _fontAtlasTextureSink;
    // Captured from the first loadFont so getFont() can lazily materialize a
    // base in the correct flavor when none is preloaded (e.g. a 13px request
    // with only a 128px SDF base cached — it must build a bitmap base, never
    // scale a tiny glyph from a huge SDF base).
    IRender*                                        _render = nullptr;
    std::unordered_map<FName, std::string>           _fontPaths;
    // Font-name-level fallback definitions. Recorded by addFontFallback so a
    // fallback face is attached not only to preloaded bases but also to bases
    // lazily materialized later (e.g. a small bitmap base auto-built for a 13px
    // request) — otherwise the late base would have an empty font stack and
    // render every non-base codepoint as tofu ('?').
    struct FFallbackDef { std::string path; EFontRenderMode mode; };
    std::unordered_map<FName, std::vector<FFallbackDef>> _fallbackDefs;
    // Missing glyphs awaiting safe-point capture (Core Rule 6): base-font ptr
    // -> codepoints. Flushed by flushPendingGlyphs at a frame boundary.
    std::unordered_map<Font*, std::unordered_set<uint32_t>> _pendingGlyphs;
    bool _bNewGlyphsCaptured = false;
    // Host-provided device-pixel scale (GUI uiScale). Bitmap glyphs are baked
    // at round(size * _activeDpiScale) so they map 1:1 to screen pixels on
    // Retina/HiDPI. SDF ignores it. Defaults to 1.0 (logical pixels).
    float _activeDpiScale = 1.0f;
    uint64_t _resourceRevision = 0;

    void bumpResourceRevision() { ++_resourceRevision; }

    [[nodiscard]] std::shared_ptr<Font> findBestBase(const FName& fontName, uint32_t fontSize) const;

    // Attaches a recorded fallback face to a single base font (builds its atlas
    // + repack callback). Shared by addFontFallback (existing bases) and
    // loadFont (lazily built bases) so every base carries the full font stack.
    void attachFallbackToBase(IRender& render, Font& font, const FFallbackDef& def);

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
    /// Load a font rasterized in `renderMode` at `fontSize`. When `mode` is
    /// nullopt the manager auto-selects per the size split (font-framework
    /// plan §1): small sizes use the hinted grayscale bitmap, larger sizes
    /// use the SDF distance field. Small glyphs never go through SDF — its
    /// 8-bit field cannot preserve thin strokes at tiny pixel sizes.
    std::shared_ptr<Font> loadFont(IRender& render, const std::string &fontPath, const FName &fontName, uint32_t fontSize,
                                   std::optional<EFontRenderMode> mode = std::nullopt, float dpiScale = 1.0f);

    /// @param dpiScale Device-pixel scale (e.g. GUI uiScale on Retina). Bitmap
    /// glyphs are rasterized at round(fontSize * dpiScale) so texels map 1:1 to
    /// screen pixels (no fractional minification under Nearest sampling — ImGui
    /// bakes at RasterizerDensity for the same reason). SDF is scale-free and
    /// ignores dpiScale. Defaults to 1.0 (logical pixels). When omitted, the
    /// manager uses the active DPI scale set by the host (setActiveDpiScale) —
    /// bitmap glyphs must be baked at the device resolution, which the host
    /// knows, not the widget.
    std::shared_ptr<Font> getFont(const FName &fontName, uint32_t fontSize, float dpiScale = 1.0f);

    /// Host sets the device-pixel scale (GUI uiScale) once per frame so bitmap
    /// glyphs are rasterized at the correct device resolution. Widgets call
    /// getFont(name, size) without dpiScale; the active scale is applied here.
    void setActiveDpiScale(float dpiScale) { _activeDpiScale = dpiScale; }
    float getActiveDpiScale() const { return _activeDpiScale; }

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

    /// The engine's default UI face: the BUNDLED proportional Latin font
    /// (Inter, OFL) when present, else empty. One entry point so every host
    /// agrees on the primary face - a host that instead picks a system CJK
    /// font as its primary gets that machine's Latin design (Hiragino/PingFang
    /// on macOS, YaHei on Windows), so chrome text differs per machine and
    /// diverges from the host that did bundle one.
    static std::string findDefaultUiFontPath();

    /// Register the fallbacks that make a Latin-primary UI face usable for
    /// CJK and emoji text: ONE CJK face plus the bundled color emoji face.
    /// Must be called after loadFont for `fontName`. Exactly one CJK fallback
    /// is registered on purpose (see findCjkFontCandidates): several faces
    /// hint the same px with different stem weights, which makes adjacent
    /// Chinese glyphs look brighter/darker than each other.
    void addDefaultUiFallbacks(IRender& render, const FName& fontName);

    /// Pre-register a font under `name:size` so getFont() returns it without
    /// loading (rasterizer + GPU not needed). Hosts that pre-build glyph data
    /// and layout tests injecting synthetic fonts use this; getFont already
    /// serves cached entries first, so production loading is unchanged.
    void registerFont(const FName &fontName, uint32_t fontSize, std::shared_ptr<Font> font);

    /// Monotonic revision bumped when a font is registered/loaded/unloaded or
    /// pending glyphs are flushed into the atlas. WidgetTree consumes this at
    /// snapshot time so layout/paint run again when text metrics or atlas
    /// pages become ready. Hosts only flush glyphs; they do not re-invalidate.
    [[nodiscard]] uint64_t resourceRevision() const { return _resourceRevision; }

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

    /// Register missing glyphs of `text` for lazy capture (no GPU work). The
    /// host calls flushPendingGlyphs at a safe frame point (after snapshot
    /// build, before command recording — Core Rule 6). Missing glyphs render
    /// as '?' until the next flush (standard 1-frame latency).
    /// Returns true when NEW glyphs were registered. WidgetTree remasures on
    /// the next snapshot from resourceRevision(); hosts only flush.
    bool requestGlyphs(Font& font, std::string_view text);
    /// Rasterize + add all pending glyphs into their font's dynamic atlas
    /// (grow/repack as needed). Safe-point only. Sets an internal flag when
    /// glyphs were actually captured; consumeNewGlyphCapture() reports it.
    void flushPendingGlyphs(IRender& render);
    /// True when the last flush captured new glyphs (and clears the flag).
    /// WidgetTree already invalidates from resourceRevision(); this remains
    /// for host/debug observers.
    bool consumeNewGlyphCapture();

    /// One GPU atlas page (primary or fallback) for the Font Atlases debug tab.
    /// `label` is combo text: "{face stem}  {size}px  {Bitmap|SDF}" (role/page/dpi
    /// only when they distinguish). `detail` is stack · texels · glyphs · path.
    struct FFontAtlasDebugPage
    {
        std::string              label;
        std::string              detail;
        EFontRenderMode          renderMode = EFontRenderMode::Bitmap;
        uint32_t                 pageIndex  = 0;
        uint32_t                 pageCount  = 0;
        std::shared_ptr<Texture> texture;
    };
    /// Unique atlas pages on loaded bases (views share these textures).
    [[nodiscard]] std::vector<FFontAtlasDebugPage> collectFontAtlasDebugPages() const;

    // TODO: optimize key generation
    static std::string makeCacheKey(const FName &fontName, uint32_t fontSize)
    {
        return std::format("{}:{}", fontName.toString(), fontSize);
    }
};

} // namespace ya
