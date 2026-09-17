#include "Render/Resources/FontManager.h"
#include "BitmapFontRasterizer.h"
#include "ColorFontRasterizer.h"
#include "Core/Profiling/Instrumentor.h"
#include "Core/System/PathUtils.h"
#include "SDFFontRasterizer.h"
#include "Core/System/VirtualFileSystem.h"
#include "Render/Resources/DynamicFontAtlas.h"
#include "freetype/freetype.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <string_view>

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

float parseFloatView(std::string_view text, float fallback)
{
    if (text.empty()) {
        return fallback;
    }
    float value = fallback;
    const std::from_chars_result parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} ? value : fallback;
}

/// `_baseFontCache` keys are `name:size` (registerFont) or `name:size:dpi` (loadFont).
void splitFontCacheKey(std::string_view key, std::string& stackName, float& logicalSize, float& dpi)
{
    dpi          = 1.0f;
    logicalSize  = 0.0f;
    const auto last = key.rfind(':');
    if (last == std::string_view::npos) {
        stackName = std::string(key);
        return;
    }
    const auto prev = last == 0 ? std::string_view::npos : key.rfind(':', last - 1);
    if (prev == std::string_view::npos) {
        stackName   = std::string(key.substr(0, last));
        logicalSize = parseFloatView(key.substr(last + 1), 0.0f);
        return;
    }
    stackName   = std::string(key.substr(0, prev));
    logicalSize = parseFloatView(key.substr(prev + 1, last - prev - 1), 0.0f);
    dpi         = parseFloatView(key.substr(last + 1), 1.0f);
}

std::string atlasDebugFaceName(std::string_view facePath, std::string_view stackName)
{
    if (!facePath.empty()) {
        const std::string stem = path_utils::pathToUtf8String(
            path_utils::pathFromUtf8String(std::string(facePath)).stem());
        if (!stem.empty()) {
            return stem;
        }
    }
    return std::string(stackName);
}

std::string makeFontAtlasDebugLabel(std::string_view face,
                                    float            logicalSize,
                                    EFontRenderMode  mode,
                                    std::string_view faceRole,
                                    uint32_t         pageIndex,
                                    uint32_t         pageCount,
                                    float            dpi)
{
    std::string label = std::format("{}  {:.0f}px  {}", face, logicalSize, fontRenderModeName(mode));
    if (faceRole != "primary") {
        label += "  ";
        label += faceRole;
    }
    if (pageCount > 1) {
        label += std::format("  p{}/{}", pageIndex + 1, pageCount);
    }
    if (std::abs(dpi - 1.0f) > 0.01f) {
        label += std::format("  @{:g}x", dpi);
    }
    return label;
}

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
    const std::string baseKey = makeCacheKey(fontName, fontSize);
    _baseFontCache[baseKey] = font;
    _fontCache[baseKey] = font;
    // getFont() uses a DPI-qualified cache key so bitmap glyphs cannot be
    // reused across device scales. Pre-registered/headless fonts have no
    // rasterization path, so register the current active scale explicitly;
    // otherwise every lookup misses the pre-registered font and returns null.
    _fontCache[baseKey + std::format(":{}", _activeDpiScale)] = font;
    auto& sizes = _baseSizes[fontName];
    if (std::find(sizes.begin(), sizes.end(), fontSize) == sizes.end()) {
        sizes.push_back(fontSize);
    }
    bumpResourceRevision();
}

std::shared_ptr<Font> FontManager::findBestBase(const FName &fontName, uint32_t fontSize) const
{
    auto sizeIt = _baseSizes.find(fontName);
    if (sizeIt == _baseSizes.end() || sizeIt->second.empty()) {
        return nullptr;
    }

    // Only consider bases rasterized in the SAME flavor as the requested size.
    // A 13px request must resolve through a bitmap base, never be scaled from a
    // 64px SDF base (that would re-introduce the SDF small-glyph bite-out).
    const EFontRenderMode wantMode = chooseModeForSize(fontSize);

    std::shared_ptr<Font> bestBase;
    uint64_t              bestScore = std::numeric_limits<uint64_t>::max();
    for (uint32_t baseSize : sizeIt->second) {
        auto baseIt = _baseFontCache.find(makeCacheKey(fontName, baseSize));
        if (baseIt == _baseFontCache.end() || !baseIt->second) {
            continue;
        }
        if (baseIt->second->renderMode != wantMode) {
            continue;
        }
        // Prefer a base that is >= requested size (down-scaling loses less
        // information than up-scaling for SDF), then minimise the size gap.
        const uint64_t gap = (baseSize > fontSize)
                               ? static_cast<uint64_t>(baseSize - fontSize)
                               : static_cast<uint64_t>(fontSize - baseSize);
        const uint64_t score = (baseSize >= fontSize ? 0u : 1000u) + gap;
        if (score < bestScore) {
            bestScore = score;
            bestBase  = baseIt->second;
        }
    }
    return bestBase;
}

std::shared_ptr<Font> FontManager::getFont(const FName &fontName, uint32_t fontSize, float dpiScale)
{
    // When no explicit dpiScale is passed (default 1.0), use the host-provided
    // active device scale so bitmap glyphs are baked at the device resolution.
    const float effectiveDpi = (dpiScale != 1.0f) ? dpiScale : _activeDpiScale;
    // Fast path: exact-size view or base already materialized. The raster size
    // of a bitmap glyph depends on dpiScale, so the cache key must include it.
    const std::string key = makeCacheKey(fontName, fontSize) + std::format(":{}", effectiveDpi);
    if (auto it = _fontCache.find(key); it != _fontCache.end()) {
        return it->second;
    }

    // Bitmap flavor MUST be rasterized at the exact display size. A hinted
    // coverage glyph is pixel-aligned by construction; serving a 16px request
    // from a 13px base via a scaled view stretches the bitmap by a
    // non-integer factor (16/13), and nearest sampling at those fractional
    // positions skips stroke texels — thin bars render broken/narrow and
    // diagonal tips drop. So for bitmap sizes only an EXACT-size base may
    // serve the request; anything else lazily rasterizes a new base. dpiScale
    // bakes the raster at the device size so texels map 1:1 to screen pixels
    // (no fractional minification — ImGui bakes at RasterizerDensity).
    if (chooseModeForSize(fontSize) == EFontRenderMode::Bitmap) {
        // If the family already has a registered base, prefer returning a
        // scaled view from that base. This keeps synthetic / pre-registered
        // font tests working even when there is no file-backed rasterization
        // path to lazily materialize from.
        if (auto base = findBestBase(fontName, fontSize)) {
            auto view = makeScaledView(base, fontSize);
            _fontCache[key] = view;
            return view;
        }

        // A base rasterized at a different dpiScale cannot be reused (its glyph
        // texels are sized for another device resolution). Lazily materialize a
        // dpi-aware base instead.
        auto pathIt = _fontPaths.find(fontName);
        if (_render && pathIt != _fontPaths.end()) {
            YA_CORE_INFO("FontManager: lazily building bitmap base '{}' size {} dpiScale {} (exact-size device raster)",
                         fontName.toString(), fontSize, effectiveDpi);
            return loadFont(*_render, pathIt->second, fontName, fontSize, std::nullopt, effectiveDpi);
        }
        YA_CORE_WARN("Font '{}' not in cache. Call loadFont first.", fontName.toString());
        return nullptr;
    }

    // SDF flavor: the distance field is scale-free by design, so the closest
    // SDF base serves the request through a scaled view.
    auto base = findBestBase(fontName, fontSize);
    if (base) {
        // The view always carries the LOGICAL size. The draw side multiplies by
        // uiScale to reach the device footprint. Bitmap bases are rasterized at
        // the device size (so the atlas texel count == device footprint), and
        // rescaleCharacter's view/base factor folds that device raster back down
        // to logical; the net result is a 1:1 texel->pixel map on screen. SDF
        // carries the logical size directly (its field is scale-free).
        auto view = makeScaledView(base, fontSize);
        _fontCache[key] = view;
        return view;
    }

    // No preloaded SDF base (e.g. only small bitmap bases exist). Lazily
    // materialize one; loadFont rasterizes SDF at the 64px base and returns a
    // scaled view for the requested size.
    auto pathIt = _fontPaths.find(fontName);
    if (_render && pathIt != _fontPaths.end()) {
        YA_CORE_INFO("FontManager: lazily building '{}' size {} (no preloaded base in that flavor)",
                     fontName.toString(), fontSize);
        return loadFont(*_render, pathIt->second, fontName, fontSize, std::nullopt, effectiveDpi);
    }

    YA_CORE_WARN("Font '{}' not in cache. Call loadFont first.", fontName.toString());
    return nullptr;
}

void FontManager::unloadFont(const FName &fontName, uint32_t fontSize)
{
    std::string cacheKey = makeCacheKey(fontName, fontSize);
    auto        it       = _fontCache.find(cacheKey);
    if (it != _fontCache.end()) {
        _fontCache.erase(it);
        YA_CORE_INFO("Unloaded font '{}' size {}", fontName.toString(), fontSize);
        bumpResourceRevision();
    }
}

void FontManager::clearCache()
{
    _fontCache.clear();
    _baseFontCache.clear();
    _baseSizes.clear();
    _pendingGlyphs.clear();
    _fontPaths.clear();
    // _render is a non-owning observer; a fresh loadFont will re-capture it.
    bumpResourceRevision();
    YA_CORE_INFO("Cleared all font cache");
}

std::shared_ptr<Font> FontManager::loadFont(IRender& render, const std::string &fontPath, const FName &fontName, uint32_t fontSize,
                                            std::optional<EFontRenderMode> mode, float dpiScale)
{
    YA_PROFILE_FUNCTION_LOG();
    // When no explicit dpiScale is passed (default 1.0), use the host-provided
    // active device scale so bitmap glyphs are baked at the device resolution.
    const float effectiveDpi = (dpiScale != 1.0f) ? dpiScale : _activeDpiScale;
    // Capture the render handle so getFont() can lazily build a base in the
    // correct flavor when none is preloaded (font-framework plan §1).
    _render = &render;
    _fontPaths[fontName] = fontPath;

    // Idempotent per (name, size, dpiScale): callers can materialize multiple
    // bases for the same font family; getFont() picks the closest base and
    // scales from it. Bitmap raster size depends on dpiScale, so it is part of
    // the key.
    const std::string baseKey = makeCacheKey(fontName, fontSize) + std::format(":{}", effectiveDpi);
    if (auto it = _baseFontCache.find(baseKey); it != _baseFontCache.end()) {
        return it->second;
    }

    // Size-driven flavor split (font-framework plan §1): small glyphs use the
    // hinted grayscale bitmap (crisp, no SDF bite-out); larger glyphs use the
    // SDF distance field. An explicit mode overrides the auto split.
    const EFontRenderMode chosenMode = mode.value_or(chooseModeForSize(fontSize));

    // SDF needs a rasterization base larger than the target to leave distance-
    // field headroom; the glyphs are then served through a scaled view. Bitmap
    // is rasterized at the exact device size (hinting nails strokes to pixels).
    // dpiScale bakes bitmap at round(size * dpiScale) so on-screen texels map
    // 1:1 to device pixels (no fractional minification under Nearest sampling).
    const uint32_t rasterSize = (chosenMode == EFontRenderMode::SDF)
                                    ? std::max(kSdfBaseSize, fontSize)
                                    : static_cast<uint32_t>(std::max(1L, std::lround(fontSize * effectiveDpi)));

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

    FT_Set_Pixel_Sizes(face, 0, rasterSize);

    auto font        = std::make_shared<Font>();
    font->fontSize   = (float)rasterSize;
    font->fontPath   = fontPath;
    font->lineHeight = (float)(face->size->metrics.height >> 6);    // 26.6 fixed point to integer
    font->ascent     = (float)(face->size->metrics.ascender >> 6);  // Distance from baseline to top
    font->descent    = (float)(face->size->metrics.descender >> 6); // Distance from baseline to bottom (negative)

    // First pass: calculate max glyph dimensions for the seed atlas.
    uint32_t maxGlyphWidth  = 0;
    uint32_t maxGlyphHeight = 0;

    for (uint32_t codePoint : BASE_GLYPH_CODEPOINTS) {
        if (FT_Load_Char(face,
                         static_cast<FT_ULong>(codePoint),
                         FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_FORCE_AUTOHINT)) {
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

    YA_CORE_INFO("Font atlas seed dimensions of {}: {}x{} (maxGlyph={}x{}), mode={}, rasterSize: {}",
                 fontName.toString(),
                 atlasWidth,
                 atlasHeight,
                 maxGlyphWidth,
                 maxGlyphHeight,
                 (int)chosenMode,
                 rasterSize);

    // Rasterizer + growable atlas (single texture; repack on growth).
    // SDF mode: FreeType distance field, scale-free (crisp at any size).
    if (chosenMode == EFontRenderMode::SDF) {
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
    // Paged bank: one page of seedSize; overflow appends a new page instead of
    // hitting a hard ceiling (CJK-heavy text never drops glyphs).
    font->atlas      = std::make_shared<FontAtlasBank>(render, font->rasterizer->getAtlasFormat(), seedSize, atlasLabel);
    font->atlasTexture = font->atlas->pageTexture(0);
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
        rawFont->atlasTexture = rawFont->atlas->pageTexture(0);
        if (_fontAtlasTextureSink) {
            _fontAtlasTextureSink(FName("RuntimeDefault"), static_cast<uint32_t>(rawFont->fontSize), rawFont->atlas->pageTexture(0));
        }
    });

    // Second pass: rasterize the base ASCII run into the dynamic atlas.
    for (uint32_t codePoint : BASE_GLYPH_CODEPOINTS) {
        GlyphBitmap glyph = font->rasterizer->rasterize(face, codePoint, rasterSize);
        Character   character;
        character.size       = {static_cast<int>(glyph.width), static_cast<int>(glyph.height)};
        character.bearing    = glyph.bearing;
        character.advance    = glyph.advance;
        character.designSize = rasterSize;
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
    font->atlasTexture = font->atlas->pageTexture(0);
    if (_fontAtlasTextureSink) {
        _fontAtlasTextureSink(fontName, rasterSize, font->atlas->pageTexture(0));
    }

    // Cache the base font and its exact-size fast path.
    _baseFontCache[baseKey] = font;
    _fontCache[baseKey] = font;
    auto& sizes = _baseSizes[fontName];
    if (std::find(sizes.begin(), sizes.end(), rasterSize) == sizes.end()) {
        sizes.push_back(rasterSize);
    }
    bumpResourceRevision();

    YA_CORE_INFO("Loaded font '{}' (mode={}, rasterSize: {}, atlas: {}x{})", fontName.toString(), (int)chosenMode, rasterSize, atlasWidth, atlasHeight);

    // Attach any recorded fallback faces to this base so its font stack is
    // complete — covers both preloaded bases and lazily materialized ones
    // (e.g. a small bitmap base auto-built for a 13px request), which would
    // otherwise render non-base codepoints as tofu.
    if (_render) {
        if (auto defIt = _fallbackDefs.find(fontName); defIt != _fallbackDefs.end()) {
            for (const FFallbackDef& def : defIt->second) {
                attachFallbackToBase(*_render, *font, def);
            }
        }
    }

    // Every request is served through a display-size scaled view. The view
    // carries the LOGICAL size; the draw side multiplies by uiScale to reach
    // the device footprint. Bitmap bases are rasterized at the device size
    // (atlas texel count == device footprint) and rescaleCharacter folds that
    // device raster back down to logical, so the net on-screen map is 1:1 texel
    // -> pixel (ImGui bakes at RasterizerDensity for the same reason). SDF
    // bases are larger than the target (distance-field headroom) and carry the
    // logical size directly (scale-free). The view layer is still required
    // because fallback glyphs are rasterized at their OWN base sizes (e.g. 64px
    // SDF CJK attached to a 13px bitmap base) and need per-glyph
    // fontSize/designSize scaling in rescaleCharacter.
    // The view carries the LOGICAL size; the draw side multiplies by uiScale to
    // reach the device footprint. Bitmap bases are rasterized at the device
    // size (atlas texel count == device footprint) and rescaleCharacter folds
    // that back to logical, giving a 1:1 texel->pixel map on screen. SDF carries
    // the logical size directly.
    auto view = makeScaledView(font, fontSize);
    _fontCache[makeCacheKey(fontName, fontSize) + std::format(":{}", effectiveDpi)] = view;
    return view;
}

bool FontManager::addFontFallback(IRender& render, const FName& fontName, const std::string& fontPath,
                                EFontRenderMode renderMode, uint32_t /*baseSize*/)
{
    auto sizeIt = _baseSizes.find(fontName);
    if (sizeIt == _baseSizes.end() || sizeIt->second.empty()) {
        YA_CORE_WARN("addFontFallback: font '{}' not loaded yet", fontName.toString());
        return false;
    }

    // Record the definition at the font-name level so lazily materialized bases
    // (e.g. a small bitmap base built on first 13px request) also get it.
    // Same path twice would split one script across duplicate faces.
    auto& defs = _fallbackDefs[fontName];
    for (const FFallbackDef& existing : defs) {
        if (existing.path == fontPath) {
            return true;
        }
    }
    defs.push_back({fontPath, renderMode});
    _render = &render;

    // Attach the fallback face to every existing base of this font.
    bool anyAdded = false;
    for (uint32_t primarySize : sizeIt->second) {
        auto baseIt = _baseFontCache.find(makeCacheKey(fontName, primarySize));
        if (baseIt == _baseFontCache.end() || !baseIt->second) {
            continue;
        }
        attachFallbackToBase(render, *baseIt->second, defs.back());
        anyAdded = true;
    }
    if (anyAdded) {
        YA_CORE_INFO("Font '{}' fallback added: '{}' (mode={})", fontName.toString(), fontPath, (int)renderMode);
        return true;
    }
    YA_CORE_WARN("addFontFallback: no base fonts found for '{}'", fontName.toString());
    return false;
}

void FontManager::attachFallbackToBase(IRender& render, Font& font, const FFallbackDef& def)
{
    // Attach a fallback face to a single base, in the SAME flavor as the base:
    // a small bitmap base gets a hinted bitmap CJK fallback rasterized at the
    // exact display size (a 64px SDF field minified to 13px drops 1px strokes
    // — the sampler grid is coarser than the stroke), while an SDF base gets
    // an SDF fallback rasterized at the SDF base size (distance-field
    // headroom). Emoji stays Color (opaque RGBA) regardless. The per-glyph
    // display scaling (fontSize / designSize) happens in rescaleCharacter when
    // the font is served through a scaled view.
    const EFontRenderMode effectiveMode = def.mode == EFontRenderMode::Color
                                              ? EFontRenderMode::Color
                                              : font.renderMode;
    FFontStackEntry entry;
    entry.fontPath   = def.path;
    entry.renderMode = effectiveMode;
    entry.baseSize   = effectiveMode == EFontRenderMode::SDF
                           ? std::max(kSdfBaseSize, static_cast<uint32_t>(font.fontSize))
                           : static_cast<uint32_t>(font.fontSize);
    entry.rasterizer = makeRasterizer(effectiveMode);
    // Color emoji needs its own atlas (opaque RGBA); the atlas label must
    // match the flavor so the sampler routing picks nearest (bitmap) or
    // linear (SDF) accordingly.
    const std::string label = effectiveMode == EFontRenderMode::Color ? "ColorFontAtlas"
                              : effectiveMode == EFontRenderMode::SDF ? "SDFFontAtlas_Fallback"
                                                                       : "FontAtlas_Fallback";
    const uint32_t fallbackSeed = effectiveMode == EFontRenderMode::Color ? 256u : 1024u;
    entry.atlas = std::make_shared<FontAtlasBank>(render, entry.rasterizer->getAtlasFormat(), fallbackSeed, label);
    entry.atlas->upload();
    entry.atlasTexture = entry.atlas->pageCount() ? entry.atlas->pageTexture(0) : nullptr;
    font.fallbacks.push_back(std::move(entry));
    const size_t fallbackIndex = font.fallbacks.size() - 1;
    font.fallbacks[fallbackIndex].atlas->setOnRepack([rawFont = &font, fallbackIndex]() {
        if (fallbackIndex >= rawFont->fallbacks.size()) {
            return;
        }
        auto& fallback = rawFont->fallbacks[fallbackIndex];
        if (!fallback.atlas) {
            return;
        }
        const uint16_t atlasIndex = static_cast<uint16_t>(fallbackIndex + 1);
        for (auto& [codePoint, character] : rawFont->characters) {
            (void)codePoint;
            if (character.atlasIndex == atlasIndex && character.atlasSlot != ~0u) {
                character.uvRect = fallback.atlas->getUv(character.atlasSlot);
            }
        }
        fallback.atlasTexture = fallback.atlas->pageTexture(0);
    });
}

std::vector<std::string> FontManager::findCjkFontCandidates()
{
    // Ordered best-first. The first path that exists on the platform is used
    // as the SINGLE CJK fallback (registered once), so a run of Chinese glyphs
    // always comes from one font face — keeping stroke weight / brightness
    // consistent. Multiple CJK fallbacks must NOT be registered together:
    // different faces hint at 13px with different stem weights, which makes
    // adjacent characters look brighter/darker than each other.
    //
    // We prefer a full-coverage, style-uniform native CJK face (PingFang on
    // macOS, Microsoft YaHei on Windows) over the partial-coverage system
    // fonts (AppleGothic / AquaKana / Hiragino only cover subsets), and keep
    // Noto/Source Han as the project-bundled fallback when no native face is
    // present.
    return {
        "/System/Library/Fonts/PingFang.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "Engine/Content/Fonts/NotoSansSC-Regular.otf",
        "Engine/Content/Fonts/SourceHanSansSC-Regular.otf",
        "C:/Windows/Fonts/simhei.ttf",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
        "/System/Library/Fonts/Supplemental/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/AquaKana.ttc",
        "/System/Library/Fonts/Supplemental/NotoSansGothic-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
    };
}

std::string FontManager::findEmojiFontPath()
{
    constexpr const char* kEmojiPath = "Engine/Content/Fonts/seguiemj.ttf";
    return std::filesystem::exists(kEmojiPath) ? std::string(kEmojiPath) : std::string{};
}

std::string FontManager::findDefaultUiFontPath()
{
    // Bundled so text metrics are identical on every machine. Chrome uses a
    // PROPORTIONAL face: a monospace primary makes every label, menu and field
    // read as terminal output, and its fixed advance wastes width in dense
    // tool panels. JetBrains Mono stays in the repo for code/console surfaces.
    return resolveUiFontFacePath(defaultUiFontFace());
}

namespace
{
/// Platform UI face used only by the `system` catalog entry. Kept as a
/// candidate list because the path differs per OS and per macOS version.
constexpr const char* kSystemUiFontCandidates[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/segoeui.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
};
} // namespace

std::span<const FUiFontFace> uiFontFaces()
{
    // Order = preference, and index 0 is the engine default. Only entries that
    // answer a real need earn a slot: the bundled proportional face (the
    // shipped look), the bundled monospace (column-aligned readouts), and the
    // platform's own face for users who want the OS look and accept that its
    // metrics differ from machine to machine.
    static constexpr FUiFontFace kFaces[] = {
        {.id          = "inter",
         .label       = "Inter (bundled)",
         .bundledPath = "Engine/Content/Fonts/Inter-Regular.ttf"},
        {.id          = "jetbrains-mono",
         .label       = "JetBrains Mono (bundled)",
         .bundledPath = "Engine/Content/Fonts/JetBrainsMono-Medium.ttf",
         .bMonospace  = true},
        {.id         = "system",
         .label      = "System UI",
         .systemPath = kSystemUiFontCandidates[0]},
    };
    return kFaces;
}

const FUiFontFace& defaultUiFontFace()
{
    return uiFontFaces().front();
}

const FUiFontFace* findUiFontFace(std::string_view id)
{
    for (const FUiFontFace& face : uiFontFaces()) {
        if (face.id == id) {
            return &face;
        }
    }
    return nullptr;
}

std::string resolveUiFontFacePath(const FUiFontFace& face)
{
    if (!face.bundledPath.empty() && std::filesystem::exists(face.bundledPath)) {
        return std::string(face.bundledPath);
    }
    if (!face.systemPath.empty()) {
        // The `system` entry names one representative path; probe the rest of
        // the platform list so a macOS that dropped HelveticaNeue still resolves.
        for (const char* candidate : kSystemUiFontCandidates) {
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

void FontManager::addDefaultUiFallbacks(IRender& render, const FName& fontName)
{
    for (const std::string& cjkPath : findCjkFontCandidates()) {
        if (std::filesystem::exists(cjkPath)) {
            addFontFallback(render, fontName, cjkPath, EFontRenderMode::Bitmap, 13);
            break; // single CJK fallback only
        }
    }
    if (const std::string emojiPath = findEmojiFontPath(); !emojiPath.empty()) {
        addFontFallback(render, fontName, emojiPath, EFontRenderMode::Color, 32);
    }
}

bool FontManager::loadUiFontStack(IRender& render, std::string_view faceId, uint32_t primarySize)
{
    const FUiFontFace* face = findUiFontFace(faceId);
    if (!face) {
        YA_CORE_WARN("FontManager: unknown UI font face '{}'; keeping the loaded stack", faceId);
        return false;
    }
    const std::string primaryPath = resolveUiFontFacePath(*face);
    if (primaryPath.empty()) {
        YA_CORE_WARN("FontManager: UI font face '{}' is not available on this machine", faceId);
        return false;
    }

    // Replace, do not add: the face is identified by its name, and loadFont is
    // idempotent per (name, size, dpi), so reloading the same name would hand
    // back the previously rasterized face. Drop every registration this name
    // owns - cached views, bases, its size list, its fallback defs and the
    // recorded path - then load. Dropping the cached VIEWS matters as much as
    // the bases: they hold shared_ptr to the old bases, so a surviving view
    // keeps the previous face alive and getFont would keep serving it.
    const FName primaryName(DEFAULT_RUNTIME_FONT_NAME);
    const std::string prefix = primaryName.toString() + ":";
    for (auto it = _fontCache.begin(); it != _fontCache.end();) {
        it = it->first.starts_with(prefix) ? _fontCache.erase(it) : std::next(it);
    }
    for (auto it = _baseFontCache.begin(); it != _baseFontCache.end();) {
        it = it->first.starts_with(prefix) ? _baseFontCache.erase(it) : std::next(it);
    }
    _baseSizes.erase(primaryName);
    _fontPaths.erase(primaryName);
    _fallbackDefs.erase(primaryName);

    if (!loadFont(render, primaryPath, primaryName, primarySize)) {
        YA_CORE_WARN("FontManager: failed to rasterize UI face '{}' from '{}'", faceId, primaryPath);
        return false;
    }
    addDefaultUiFallbacks(render, primaryName);

    // The monospace family is part of the UI stack, not a side quest: without it
    // a widget that names it would draw nothing. Missing is not fatal - the
    // primary face still serves the whole shell - so it only warns.
    if (const FUiFontFace* mono = findUiFontFace("jetbrains-mono")) {
        if (const std::string monoPath = resolveUiFontFacePath(*mono); !monoPath.empty()) {
            const FName monoName(MONO_UI_FONT_NAME);
            if (_fontPaths.find(monoName) == _fontPaths.end()) {
                loadFont(render, monoPath, monoName, primarySize);
                addDefaultUiFallbacks(render, monoName);
            }
        } else {
            YA_CORE_WARN("FontManager: bundled monospace face is missing; mono-family text will not render");
        }
    }

    // Text metrics changed, so trees that cached measured sizes have to re-run
    // layout. WidgetTree already invalidates from resourceRevision(); the host
    // only has to swap the face.
    bumpResourceRevision();
    YA_CORE_INFO("FontManager: UI font stack = '{}' ({}) @ {}px + mono", faceId, primaryPath, primarySize);
    return true;
}

bool FontManager::requestGlyphs(Font& font, std::string_view text)
{
    Font& target = font.isView() ? *font.baseFont : font;
    if (!target.rasterizer || !target.atlas || target.fontPath.empty()) {
        return false; // synthetic/registered fonts own their glyph set; nothing to capture
    }
    bool bNew = false;
    for (uint32_t codePoint : utf8::decode(text)) {
        if (codePoint == '\r' || codePoint == '\n' || codePoint == '\t' || utf8::isIgnorableFormatCodePoint(codePoint)) {
            continue;
        }
        if (target.characters.contains(codePoint)) {
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
    bumpResourceRevision();
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
        auto captureInto = [&](FT_Face face, IFontRasterizer& rasterizer, FontAtlasBank& atlas,
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
                const uint32_t slot = atlas.addGlyph(glyph.width, glyph.height, glyph.pixels.data());
                if (slot == ~0u) {
                    return false; // atlas could not pack it (hit size ceiling) — let another face try
                }
                character.atlasSlot = slot;
                character.uvRect    = atlas.getUv(slot);
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
        // Some platform fonts are TTC containers with multiple faces; scanning
        // all faces avoids missing glyphs that live outside face 0.
        for (size_t i = 0; i < font.fallbacks.size() && !remaining.empty(); ++i) {
            FFontStackEntry& fb = font.fallbacks[i];
            FT_Face fbProbe{};
            if (FT_New_Face(ft, fb.fontPath.c_str(), 0, &fbProbe)) {
                continue;
            }
            const long faceCount = std::max<long>(1, fbProbe->num_faces);
            FT_Done_Face(fbProbe);

            std::vector<uint32_t> stillMissing;
            for (uint32_t codePoint : remaining) {
                if (font.characters.contains(codePoint)) {
                    continue;
                }
                bool bCaptured = false;
                for (long faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
                    FT_Face fbFace{};
                    if (FT_New_Face(ft, fb.fontPath.c_str(), faceIndex, &fbFace)) {
                        continue;
                    }
                    // FT_Get_Char_Index: cheap presence probe before rasterizing.
                    if (FT_Get_Char_Index(fbFace, static_cast<FT_ULong>(codePoint)) != 0 &&
                        captureInto(fbFace, *fb.rasterizer, *fb.atlas, codePoint, fb.baseSize,
                                    static_cast<uint16_t>(i + 1))) {
                        bCaptured = true;
                        FT_Done_Face(fbFace);
                        break;
                    }
                    FT_Done_Face(fbFace);
                }
                if (!bCaptured) {
                    stillMissing.push_back(codePoint);
                }
            }
            remaining = std::move(stillMissing);
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
            font.atlasTexture = font.atlas->pageTexture(0);
        }
        for (FFontStackEntry& fb : font.fallbacks) {
            if (fb.atlas) {
                fb.atlas->upload();
                fb.atlasTexture = fb.atlas->pageTexture(0);
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

    // Load the atlas at the adapted size itself so getFont() can serve an
    // exact-size base instead of scaling a huge base down to tiny text.
    loadFont(render, fontPath, fontName, adaptedSize);
    return getFont(fontName, adaptedSize);
}

std::vector<FontManager::FFontAtlasDebugPage> FontManager::collectFontAtlasDebugPages() const
{
    std::vector<FFontAtlasDebugPage> pages;

    const auto appendBank = [&](const FontAtlasBank* bank,
                                std::string_view     cacheKey,
                                float                fontSize,
                                std::string_view     faceRole,
                                std::string_view     facePath,
                                EFontRenderMode      mode,
                                size_t               glyphCount) {
        if (!bank || bank->pageCount() == 0) {
            return;
        }
        std::string stackName;
        float       logicalSize = 0.0f;
        float       dpi         = 1.0f;
        splitFontCacheKey(cacheKey, stackName, logicalSize, dpi);
        if (logicalSize <= 0.0f) {
            logicalSize = fontSize;
        }
        const std::string face = atlasDebugFaceName(facePath, stackName);
        const uint32_t pageCount = static_cast<uint32_t>(bank->pageCount());
        for (uint32_t i = 0; i < pageCount; ++i) {
            FFontAtlasDebugPage page;
            page.texture    = bank->pageTexture(i);
            page.pageIndex  = i;
            page.pageCount  = pageCount;
            page.renderMode = mode;
            const uint32_t w = page.texture ? page.texture->getWidth() : bank->pageSize();
            const uint32_t h = page.texture ? page.texture->getHeight() : bank->pageSize();
            page.label = makeFontAtlasDebugLabel(face, logicalSize, mode, faceRole, i, pageCount, dpi);
            page.detail = std::format("{}  ·  {}×{}  ·  {} glyphs  ·  1:1",
                                      stackName,
                                      w,
                                      h,
                                      glyphCount);
            if (!facePath.empty()) {
                page.detail += "  ·  ";
                page.detail += facePath;
            }
            pages.push_back(std::move(page));
        }
    };

    for (const auto& [key, font] : _baseFontCache) {
        if (!font || font->isView()) {
            continue;
        }
        size_t primaryGlyphs = 0;
        for (const auto& [codePoint, ch] : font->characters) {
            (void)codePoint;
            if (ch.atlasIndex == 0) {
                ++primaryGlyphs;
            }
        }
        appendBank(font->atlas.get(),
                   key,
                   font->fontSize,
                   "primary",
                   font->fontPath,
                   font->renderMode,
                   primaryGlyphs);
        for (size_t fallback = 0; fallback < font->fallbacks.size(); ++fallback) {
            const FFontStackEntry& entry = font->fallbacks[fallback];
            size_t fallbackGlyphs = 0;
            const uint16_t atlasIndex = static_cast<uint16_t>(fallback + 1);
            for (const auto& [codePoint, ch] : font->characters) {
                (void)codePoint;
                if (ch.atlasIndex == atlasIndex) {
                    ++fallbackGlyphs;
                }
            }
            appendBank(entry.atlas.get(),
                       key,
                       font->fontSize,
                       std::format("fallback{}", fallback + 1),
                       entry.fontPath,
                       entry.renderMode,
                       fallbackGlyphs);
        }
    }

    std::sort(pages.begin(),
              pages.end(),
              [](const FFontAtlasDebugPage& a, const FFontAtlasDebugPage& b) {
                  return a.label < b.label;
              });
    return pages;
}

} // namespace ya
