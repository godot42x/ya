#include "FontManager.h"
#include "BitmapFontRasterizer.h"
#include "ColorFontRasterizer.h"
#include "Core/Profiling/Instrumentor.h"
#include "SDFFontRasterizer.h"
#include "Core/System/VirtualFileSystem.h"
#include "DynamicFontAtlas.h"
#include "freetype/freetype.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ya
{

namespace
{
constexpr std::array<uint32_t, 95> BASE_GLYPH_CODEPOINTS = [] {
    std::array<uint32_t, 95> codePoints{};
    for (size_t index = 0; index < codePoints.size(); ++index) {
        codePoints[index] = static_cast<uint32_t>(32 + index);
    }
    return codePoints;
}();
} // namespace

namespace
{

std::shared_ptr<IFontRasterizer> makeRasterizer(EFontRenderMode mode)
{
    switch (mode) {
    case EFontRenderMode::SDF:
        return std::make_shared<SDFFontRasterizer>();
    case EFontRenderMode::Color:
        return std::make_shared<ColorFontRasterizer>();
    case EFontRenderMode::Bitmap:
    case EFontRenderMode::MSDF:
    default:
        return std::make_shared<BitmapFontRasterizer>();
    }
}

void rescaleCharacter(Character& out, const Character& in, float viewFontSize)
{
    // Fallback glyphs (CJK/emoji) are rasterized at their own base size; the
    // view scale must use THAT size as the denominator, not the primary
    // base size (otherwise a 64px-captured CJK glyph drawn at a 13px view
    // from a 128px primary base would render at 6.5px).
    const float scale = (in.designSize > 0) ? (viewFontSize / static_cast<float>(in.designSize))
                                            : 1.0f;
    out           = in;
    out.size      = glm::ivec2(static_cast<int>(std::lround(in.size.x * scale)),
                               static_cast<int>(std::lround(in.size.y * scale)));
    out.bearing   = glm::ivec2(static_cast<int>(std::lround(in.bearing.x * scale)),
                               static_cast<int>(std::lround(in.bearing.y * scale)));
    out.advance   = in.advance * scale;
    // The UV rect + atlas texture stay untouched: both share the base atlas.
}

std::shared_ptr<Font> makeScaledView(const std::shared_ptr<Font>& base, uint32_t size)
{
    const float scale = static_cast<float>(size) / std::max(base->fontSize, 0.0001f);
    auto        view  = std::make_shared<Font>();
    view->fontSize    = static_cast<float>(size);
    view->lineHeight  = base->lineHeight * scale;
    view->ascent      = base->ascent * scale;
    view->descent     = base->descent * scale;
    view->fontPath    = base->fontPath;
    view->renderMode  = base->renderMode;
    view->atlasTexture = base->atlasTexture;
    view->atlas        = base->atlas;
    view->rasterizer   = base->rasterizer;
    // Share the fallback chain (CJK/emoji): entries hold shared atlas +
    // rasterizer handles, so views resolve fallback glyphs against the SAME
    // atlases as the base font (fixes fallback UVs sampled from the wrong
    // texture when the view had an empty chain).
    view->fallbacks    = base->fallbacks;
    view->baseFont    = base;
    view->scale       = scale;
    view->characters.reserve(base->characters.size());
    for (const auto& [cp, ch] : base->characters) {
        Character c;
        rescaleCharacter(c, ch, view->fontSize);
        view->characters.emplace(cp, std::move(c));
    }
    return view;
}

void refreshScaledView(Font& view)
{
    const auto base = view.baseFont;
    if (!base) {
        return;
    }
    view.characters.clear();
    for (const auto& [cp, ch] : base->characters) {
        Character c;
        rescaleCharacter(c, ch, view.fontSize);
        view.characters.emplace(cp, std::move(c));
    }
}

} // namespace

FontManager *FontManager::get()
{
    static FontManager instance;
    return &instance;
}

void FontManager::setFontAtlasTextureSink(FontAtlasTextureSink sink)
{
    get()->_fontAtlasTextureSink = std::move(sink);
}

void FontManager::registerFont(const FName &fontName, uint32_t fontSize, std::shared_ptr<Font> font)
{
    if (!font) {
        return;
    }
    font->fontSize = static_cast<float>(fontSize);
    _baseFontCache[fontName] = font;
    _fontCache[makeCacheKey(fontName, fontSize)] = std::move(font);
}

std::shared_ptr<Font> FontManager::getFont(const FName &fontName, uint32_t fontSize)
{
    // Fast path: exact-size view already materialized.
    if (auto it = _fontCache.find(makeCacheKey(fontName, fontSize)); it != _fontCache.end()) {
        return it->second;
    }

    auto baseIt = _baseFontCache.find(fontName);
    if (baseIt == _baseFontCache.end() || !baseIt->second) {
        YA_CORE_WARN("Font '{}' not in cache. Call loadFont first.", fontName.toString());
        return nullptr;
    }
    auto view = makeScaledView(baseIt->second, fontSize);
    _fontCache[makeCacheKey(fontName, fontSize)] = view;
    return view;
}

void FontManager::unloadFont(const FName &fontName, uint32_t fontSize)
{
    std::string cacheKey = makeCacheKey(fontName, fontSize);
    auto        it       = _fontCache.find(cacheKey);
    if (it != _fontCache.end()) {
        _fontCache.erase(it);
        YA_CORE_INFO("Unloaded font '{}' size {}", fontName.toString(), fontSize);
    }
}

void FontManager::clearCache()
{
    _fontCache.clear();
    _baseFontCache.clear();
    _pendingGlyphs.clear();
    YA_CORE_INFO("Cleared all font cache");
}

std::shared_ptr<Font> FontManager::loadFont(IRender& render, const std::string &fontPath, const FName &fontName, uint32_t fontSize,
                                            EFontRenderMode renderMode)
{
    YA_PROFILE_FUNCTION_LOG();
    // Idempotent: one name -> one base atlas. Callers that loop over sizes
    // only materialize the base once; getFont() scales to any requested size.
    if (auto it = _baseFontCache.find(fontName); it != _baseFontCache.end()) {
        return it->second;
    }
    FT_Library ft{};
    if (FT_Err_Ok != FT_Init_FreeType(&ft)) {
        YA_CORE_ERROR("Failed to initialize FreeType library");
        return nullptr;
    }

    FT_Face face{};
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        YA_CORE_ERROR("Failed to load font: {}", fontPath);
        FT_Done_FreeType(ft);
        return nullptr;
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);

    auto font        = std::make_shared<Font>();
    font->fontSize   = (float)fontSize;
    font->fontPath   = fontPath;
    font->lineHeight = (float)(face->size->metrics.height >> 6);    // 26.6 fixed point to integer
    font->ascent     = (float)(face->size->metrics.ascender >> 6);  // Distance from baseline to top
    font->descent    = (float)(face->size->metrics.descender >> 6); // Distance from baseline to bottom (negative)

    // First pass: calculate max glyph dimensions for the seed atlas.
    uint32_t maxGlyphWidth  = 0;
    uint32_t maxGlyphHeight = 0;

    for (uint32_t codePoint : BASE_GLYPH_CODEPOINTS) {
        if (FT_Load_Char(face, static_cast<FT_ULong>(codePoint), FT_LOAD_RENDER)) {
            continue;
        }
        FT_GlyphSlot &glyph = face->glyph;
        maxGlyphWidth       = std::max(maxGlyphWidth, glyph->bitmap.width);
        maxGlyphHeight      = std::max(maxGlyphHeight, glyph->bitmap.rows);
    }

    // Seed atlas sized for the base ASCII run (16 glyphs/row + padding, pow2).
    constexpr uint32_t glyphsPerRow = 16;
    uint32_t           atlasWidth   = glyphsPerRow * (maxGlyphWidth + 2);
    const uint32_t     totalGlyphs  = static_cast<uint32_t>(BASE_GLYPH_CODEPOINTS.size());
    uint32_t           numRows      = (totalGlyphs + glyphsPerRow - 1) / glyphsPerRow;
    uint32_t           atlasHeight  = numRows * (maxGlyphHeight + 2);

    auto nextPow2 = [](uint32_t v) -> uint32_t {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    };

    atlasWidth  = nextPow2(atlasWidth);
    atlasHeight = nextPow2(atlasHeight);
    const uint32_t seedSize = std::max(atlasWidth, atlasHeight);

    YA_CORE_INFO("Font atlas seed dimensions of {}: {}x{} (maxGlyph={}x{}), fontSize: {}",
                 fontName.toString(),
                 atlasWidth,
                 atlasHeight,
                 maxGlyphWidth,
                 maxGlyphHeight,
                 fontSize);

    // Rasterizer + growable atlas (single texture; repack on growth).
    // SDF mode: FreeType distance field, scale-free (crisp at any size).
    if (renderMode == EFontRenderMode::SDF) {
        font->rasterizer = std::make_shared<SDFFontRasterizer>();
        font->renderMode = EFontRenderMode::SDF;
    }
    else {
        font->rasterizer = std::make_shared<BitmapFontRasterizer>();
        font->renderMode = EFontRenderMode::Bitmap;
    }
    const std::string atlasLabel = font->renderMode == EFontRenderMode::SDF
                                       ? "SDFFontAtlas_RuntimeDefault"
                                       : "FontAtlas_RuntimeDefault";
    font->atlas      = std::make_shared<DynamicFontAtlas>(render, font->rasterizer->getAtlasFormat(), seedSize, atlasLabel);
    font->atlasTexture = font->atlas->texture();
    // NOTE: capture the base font by RAW pointer — the atlas is owned by the
    // font, so the font outlives the atlas; capturing a shared_ptr here would
    // create a Font -> atlas -> lambda -> Font reference cycle (leak).
    font->atlas->setOnRepack([this, rawFont = font.get()]() {
        // Repack moved every glyph: refresh the base characters' UVs (views
        // are refreshed lazily via refreshScaledView on next ensureGlyphs).
        for (auto& [cp, ch] : rawFont->characters) {
            if (ch.atlasSlot != ~0u) {
                ch.uvRect = rawFont->atlas->getUv(ch.atlasSlot);
            }
        }
        rawFont->atlasTexture = rawFont->atlas->texture();
        if (_fontAtlasTextureSink) {
            _fontAtlasTextureSink(FName("RuntimeDefault"), static_cast<uint32_t>(rawFont->fontSize), rawFont->atlasTexture);
        }
    });

    // Second pass: rasterize the base ASCII run into the dynamic atlas.
    for (uint32_t codePoint : BASE_GLYPH_CODEPOINTS) {
        GlyphBitmap glyph = font->rasterizer->rasterize(face, codePoint, fontSize);
        Character   character;
        character.size       = {static_cast<int>(glyph.width), static_cast<int>(glyph.height)};
        character.bearing    = glyph.bearing;
        character.advance    = glyph.advance;
        character.designSize = fontSize;
        if (glyph.width > 0 && glyph.height > 0) {
            character.atlasSlot = font->atlas->addGlyph(glyph.width, glyph.height, glyph.pixels.data());
            character.uvRect    = font->atlas->getUv(character.atlasSlot);
        }
        else {
            character.atlasSlot = ~0u;
        }
        font->characters[codePoint] = character;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Upload the seed atlas at a safe point (right now: font load happens at
    // host init, outside any recording).
    font->atlas->upload();
    font->atlasTexture = font->atlas->texture();
    if (_fontAtlasTextureSink) {
        _fontAtlasTextureSink(fontName, fontSize, font->atlasTexture);
    }

    // Cache the base font and its exact-size fast path.
    _baseFontCache[fontName] = font;
    _fontCache[makeCacheKey(fontName, fontSize)] = font;

    YA_CORE_INFO("Loaded font '{}' (size: {}, atlas: {}x{})", fontName.toString(), fontSize, atlasWidth, atlasHeight);
    return font;
}

bool FontManager::addFontFallback(IRender& render, const FName& fontName, const std::string& fontPath,
                                EFontRenderMode renderMode, uint32_t baseSize)
{
    auto baseIt = _baseFontCache.find(fontName);
    if (baseIt == _baseFontCache.end() || !baseIt->second) {
        YA_CORE_WARN("addFontFallback: font '{}' not loaded yet", fontName.toString());
        return false;
    }
    Font& font = *baseIt->second;
    FFontStackEntry entry;
    entry.fontPath   = fontPath;
    entry.renderMode = renderMode;
    entry.baseSize   = baseSize;
    entry.rasterizer = makeRasterizer(renderMode);
    // Color emoji needs its own atlas (opaque RGBA); CJK SDF gets its own
    // atlas at a smaller base size (bounded memory).
    const std::string label = renderMode == EFontRenderMode::Color ? "ColorFontAtlas"
                                                                   : "SDFFontAtlas_Fallback";
    entry.atlas = std::make_shared<DynamicFontAtlas>(render, entry.rasterizer->getAtlasFormat(), 256, label);
    entry.atlas->upload();
    entry.atlasTexture = entry.atlas->texture();
    font.fallbacks.push_back(std::move(entry));
    YA_CORE_INFO("Font '{}' fallback added: '{}' (mode={})", fontName.toString(), fontPath, (int)renderMode);
    return true;
}

std::vector<std::string> FontManager::findCjkFontCandidates()
{
    return {
        "Engine/Content/Fonts/NotoSansSC-Regular.otf",
        "Engine/Content/Fonts/SourceHanSansSC-Regular.otf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
    };
}

std::string FontManager::findEmojiFontPath()
{
    constexpr const char* kEmojiPath = "Engine/Content/Fonts/seguiemj.ttf";
    return std::filesystem::exists(kEmojiPath) ? std::string(kEmojiPath) : std::string{};
}

void FontManager::ensureGlyphs(IRender& render, Font& font, std::string_view text)
{
    // Legacy entry point: register + flush immediately. Kept for callers that
    // load glyphs eagerly outside a recording; GUI draw paths must use
    // requestGlyphs + flushPendingGlyphs (Core Rule 6).
    requestGlyphs(font, text);
    flushPendingGlyphs(render);
}

bool FontManager::requestGlyphs(Font& font, std::string_view text)
{
    Font& target = font.isView() ? *font.baseFont : font;
    if (!target.rasterizer || !target.atlas || target.fontPath.empty()) {
        return false; // synthetic/registered fonts own their glyph set; nothing to capture
    }
    bool bNew = false;
    for (uint32_t codePoint : utf8::decode(text)) {
        if (codePoint == '\r' || codePoint == '\n' || codePoint == '\t') {
            continue;
        }
        if (target.characters.contains(codePoint) || target.missing.contains(codePoint)) {
            continue;
        }
        bNew = _pendingGlyphs[&target].insert(codePoint).second || bNew;
    }
    return bNew;
}

bool FontManager::consumeNewGlyphCapture()
{
    const bool b = _bNewGlyphsCaptured;
    _bNewGlyphsCaptured = false;
    return b;
}

void FontManager::flushPendingGlyphs(IRender& render)
{
    if (_pendingGlyphs.empty()) {
        return;
    }
    _bNewGlyphsCaptured = true;
    FT_Library ft{};
    if (FT_Err_Ok != FT_Init_FreeType(&ft)) {
        YA_CORE_ERROR("Failed to initialize FreeType library for glyph fallback");
        return;
    }

    for (auto& [fontPtr, codePoints] : _pendingGlyphs) {
        Font& font = *fontPtr;
        if (!font.rasterizer || !font.atlas || font.fontPath.empty()) {
            continue;
        }

        // Resolve each missing codepoint through the font stack: primary face
        // first, then each fallback (CJK / emoji) in order. The first face
        // that has the glyph rasterizes it into its own atlas.
        auto captureInto = [&](FT_Face face, IFontRasterizer& rasterizer, DynamicFontAtlas& atlas,
                               uint32_t codePoint, uint32_t pixelSize, uint16_t atlasIndex) {
            GlyphBitmap glyph = rasterizer.rasterize(face, codePoint, pixelSize);
            if (glyph.width == 0 && glyph.height == 0) {
                return false; // face has no renderable glyph for this cp
            }
            Character character;
            character.size       = {static_cast<int>(glyph.width), static_cast<int>(glyph.height)};
            character.bearing    = glyph.bearing;
            character.advance    = glyph.advance;
            character.atlasIndex = atlasIndex;
            character.designSize = pixelSize;
            character.bColor     = glyph.bColor;
            if (glyph.width > 0 && glyph.height > 0) {
                character.atlasSlot = atlas.addGlyph(glyph.width, glyph.height, glyph.pixels.data());
                character.uvRect    = atlas.getUv(character.atlasSlot);
            }
            else {
                character.atlasSlot = ~0u;
            }
            font.characters[codePoint] = character;
            return true;
        };

        std::vector<uint32_t> remaining;
        FT_Face face{};
        const bool bPrimaryFace = FT_New_Face(ft, font.fontPath.c_str(), 0, &face) == 0;
        for (uint32_t codePoint : codePoints) {
            if (font.characters.contains(codePoint)) {
                continue;
            }
            // Presence probe BEFORE rasterizing: FT_Load_Char happily loads
            // the .notdef glyph for missing codepoints, which would capture
            // tofu boxes instead of falling through the font stack.
            if (bPrimaryFace && FT_Get_Char_Index(face, static_cast<FT_ULong>(codePoint)) != 0 &&
                captureInto(face, *font.rasterizer, *font.atlas, codePoint,
                            static_cast<uint32_t>(font.fontSize), 0)) {
                continue;
            }
            remaining.push_back(codePoint);
        }
        if (bPrimaryFace) {
            FT_Done_Face(face);
        }

        // Fallback chain: try each fallback face for the still-missing cps.
        for (size_t i = 0; i < font.fallbacks.size() && !remaining.empty(); ++i) {
            FFontStackEntry& fb = font.fallbacks[i];
            FT_Face fbFace{};
            if (FT_New_Face(ft, fb.fontPath.c_str(), 0, &fbFace)) {
                continue;
            }
            std::vector<uint32_t> stillMissing;
            for (uint32_t codePoint : remaining) {
                if (font.characters.contains(codePoint)) {
                    continue;
                }
                // FT_Get_Char_Index: cheap presence probe before rasterizing.
                if (FT_Get_Char_Index(fbFace, static_cast<FT_ULong>(codePoint)) == 0) {
                    stillMissing.push_back(codePoint);
                    continue;
                }
                if (captureInto(fbFace, *fb.rasterizer, *fb.atlas, codePoint, fb.baseSize,
                                static_cast<uint16_t>(i + 1))) {
                    continue;
                }
                stillMissing.push_back(codePoint);
            }
            remaining = std::move(stillMissing);
            FT_Done_Face(fbFace);
        }
        // Glyphs no face could rasterize are permanently missing: never
        // re-request them (would re-run capture + atlas upload EVERY frame).
        for (uint32_t codePoint : remaining) {
            font.missing.insert(codePoint);
        }
    }

    FT_Done_FreeType(ft);

    // Re-upload every touched atlas (primary + fallbacks). Repack (if any)
    // fired onRepack already; the upload happens at this safe point.
    for (auto& [fontPtr, codePoints] : _pendingGlyphs) {
        (void)codePoints;
        Font& font = *fontPtr;
        if (font.atlas) {
            font.atlas->upload();
            font.atlasTexture = font.atlas->texture();
        }
        for (FFontStackEntry& fb : font.fallbacks) {
            if (fb.atlas) {
                fb.atlas->upload();
                fb.atlasTexture = fb.atlas->texture();
            }
        }
    }

    // Refresh all materialized scaled views so their characters match the
    // (possibly repacked) base atlas + fallbacks.
    for (const auto& [key, viewPtr] : _fontCache) {
        (void)key;
        if (viewPtr && viewPtr->isView()) {
            refreshScaledView(*viewPtr);
        }
    }

    _pendingGlyphs.clear();
}

std::shared_ptr<Font> FontManager::getAdaptiveFont(IRender&            render,
                                                   const std::string &fontPath,
                                                   const FName       &fontName,
                                                   uint32_t           baseSize,
                                                   uint32_t           windowHeight,
                                                   uint32_t           referenceHeight)
{
    // Calculate adapted font size based on window height
    float    scale       = static_cast<float>(windowHeight) / static_cast<float>(referenceHeight);
    uint32_t adaptedSize = static_cast<uint32_t>(static_cast<float>(baseSize) * scale);

    // Clamp to reasonable range
    adaptedSize = std::clamp(adaptedSize, 8u, 256u);

    // Load the atlas once at the fixed runtime base size, then return a
    // scaled view for the requested size (no per-window rasterization).
    loadFont(render, fontPath, fontName, DEFAULT_RUNTIME_FONT_SIZE);
    return getFont(fontName, adaptedSize);
}

} // namespace ya