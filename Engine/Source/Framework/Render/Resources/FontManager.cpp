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

/// `_baseFontCache` keys are `name:rasterPx`. A legacy `name:size:dpi` key still parses.
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
    view->family      = base->family;
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
    font->family   = fontName;
    const std::string baseKey = makeCacheKey(fontName, fontSize);
    _baseFontCache[baseKey] = font;
    _fontCache[baseKey] = font;
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

std::string sharedAtlasLabel(EFontRenderMode mode, std::string_view role)
{
    if (mode == EFontRenderMode::Color) {
        return "ColorFontAtlas";
    }
    if (mode == EFontRenderMode::SDF) {
        return role == "fallback" ? "SDFFontAtlas_Fallback" : "SDFFontAtlas_RuntimeDefault";
    }
    return role == "fallback" ? "FontAtlas_Fallback" : "FontAtlas_RuntimeDefault";
}

FontManager::FSharedFaceAtlas& FontManager::sharedFace(IRender& render, const std::string& path,
                                                      EFontRenderMode mode, std::string_view role)
{
    const std::string key = path + "\n" + std::to_string(static_cast<int>(mode));
    if (auto it = _sharedFaces.find(key); it != _sharedFaces.end()) {
        return it->second;
    }
    FSharedFaceAtlas face;
    face.path       = path;
    face.role       = std::string(role);
    face.mode       = mode;
    face.rasterizer = makeRasterizer(mode);
    face.bank       = std::make_shared<FontAtlasBank>(render,
                                                face.rasterizer->getAtlasFormat(),
                                                kSharedAtlasInitialPage,
                                                sharedAtlasLabel(mode, role),
                                                true);
    face.lru.reserve(static_cast<size_t>(kLiveRasterSizeWindow) + 1);
    FontAtlasBank* rawBank = face.bank.get();
    face.bank->setOnRepack([this, rawBank]() { refreshUvsForBank(rawBank); });
    auto [it, inserted] = _sharedFaces.emplace(key, std::move(face));
    (void)inserted;
    return it->second;
}

FontManager::FSharedFaceAtlas* FontManager::findSharedFace(const FontAtlasBank* bank)
{
    if (!bank) {
        return nullptr;
    }
    for (auto& [key, face] : _sharedFaces) {
        (void)key;
        if (face.bank.get() == bank) {
            return &face;
        }
    }
    return nullptr;
}

bool FontManager::rasterSizeStillNeeded(const FontAtlasBank* bank, uint32_t rasterPx) const
{
    if (!bank) {
        return false;
    }
    for (const auto& [key, font] : _baseFontCache) {
        (void)key;
        if (!font) {
            continue;
        }
        if (font->atlas.get() == bank && static_cast<uint32_t>(std::lround(font->fontSize)) == rasterPx) {
            return true;
        }
        for (size_t index = 0; index < font->fallbacks.size(); ++index) {
            if (font->fallbacks[index].atlas.get() != bank) {
                continue;
            }
            const uint16_t atlasIndex = static_cast<uint16_t>(index + 1);
            for (const auto& [codePoint, character] : font->characters) {
                (void)codePoint;
                if (character.atlasIndex == atlasIndex && character.designSize == rasterPx
                    && character.atlasSlot != ~0u) {
                    return true;
                }
            }
        }
    }
    return false;
}

void FontManager::refreshUvsForBank(const FontAtlasBank* bank)
{
    if (!bank) {
        return;
    }
    auto refreshBase = [&](Font& font) {
        if (font.atlas.get() == bank) {
            for (auto& [codePoint, character] : font.characters) {
                if (character.atlasIndex != 0) {
                    continue;
                }
                const uint32_t rasterPx = character.designSize != 0
                                              ? character.designSize
                                              : static_cast<uint32_t>(std::lround(font.fontSize));
                uint32_t slot = 0;
                glm::vec4 uv{};
                if (bank->findGlyph(codePoint, rasterPx, slot, uv)) {
                    character.atlasSlot = slot;
                    character.uvRect    = uv;
                }
                else if (character.atlasSlot != ~0u) {
                    character.atlasSlot = ~0u;
                    character.uvRect    = {};
                }
            }
            font.atlasTexture = bank->pageCount() ? bank->pageTexture(0) : nullptr;
            if (_fontAtlasTextureSink) {
                _fontAtlasTextureSink(font.family,
                                      static_cast<uint32_t>(std::lround(font.fontSize)),
                                      font.atlasTexture);
            }
        }
        for (size_t index = 0; index < font.fallbacks.size(); ++index) {
            FFontStackEntry& fallback = font.fallbacks[index];
            if (fallback.atlas.get() != bank) {
                continue;
            }
            const uint16_t atlasIndex = static_cast<uint16_t>(index + 1);
            fallback.atlasTexture = bank->pageCount() ? bank->pageTexture(0) : nullptr;
            for (auto& [codePoint, character] : font.characters) {
                if (character.atlasIndex != atlasIndex) {
                    continue;
                }
                const uint32_t rasterPx = character.designSize != 0
                                              ? character.designSize
                                              : static_cast<uint32_t>(std::lround(font.fontSize));
                uint32_t slot = 0;
                glm::vec4 uv{};
                if (bank->findGlyph(codePoint, rasterPx, slot, uv)) {
                    character.atlasSlot = slot;
                    character.uvRect    = uv;
                }
                else if (character.atlasSlot != ~0u) {
                    character.atlasSlot = ~0u;
                    character.uvRect    = {};
                }
            }
        }
    };
    for (auto& [key, font] : _baseFontCache) {
        (void)key;
        if (font && !font->isView()) {
            refreshBase(*font);
        }
    }
    for (auto& [key, font] : _fontCache) {
        (void)key;
        if (!font || !font->isView() || !font->baseFont) {
            continue;
        }
        const Font& base = *font->baseFont;
        bool usesBank = base.atlas.get() == bank;
        if (!usesBank) {
            for (const FFontStackEntry& fallback : base.fallbacks) {
                if (fallback.atlas.get() == bank) {
                    usesBank = true;
                    break;
                }
            }
        }
        if (usesBank) {
            refreshScaledView(*font);
        }
    }
}

void FontManager::dropUnreferencedSharedFaces()
{
    for (auto it = _sharedFaces.begin(); it != _sharedFaces.end();) {
        if (!it->second.bank || it->second.bank.use_count() == 1) {
            it = _sharedFaces.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool FontManager::evictRasterSize(FSharedFaceAtlas& face, uint32_t rasterPx)
{
    if (!face.bank) {
        return true;
    }
    struct FVictim
    {
        std::string key;
        Font*       raw = nullptr;
        FName       family;
    };
    std::vector<FVictim> victims;
    std::vector<std::shared_ptr<FontAtlasBank>> fallbackBanks;
    for (const auto& [key, font] : _baseFontCache) {
        if (!font || font->isView()) {
            continue;
        }
        if (font->atlas.get() != face.bank.get()) {
            continue;
        }
        if (static_cast<uint32_t>(std::lround(font->fontSize)) != rasterPx) {
            continue;
        }
        // Both caches hold the font, so use_count 2 means no snapshot and no
        // view still references it. A live holder keeps the size.
        if (font.use_count() > 2) {
            return false;
        }
        victims.push_back(FVictim{key, font.get(), font->family});
        for (const FFontStackEntry& fallback : font->fallbacks) {
            if (fallback.atlas && fallback.atlas.get() != face.bank.get()) {
                fallbackBanks.push_back(fallback.atlas);
            }
        }
    }
    std::vector<Font*> bases;
    bases.reserve(victims.size());
    for (const FVictim& victim : victims) {
        bases.push_back(victim.raw);
        _pendingGlyphs.erase(victim.raw);
    }
    for (auto it = _fontCache.begin(); it != _fontCache.end();) {
        if (!it->second) {
            it = _fontCache.erase(it);
            continue;
        }
        Font* raw  = it->second.get();
        Font* base = it->second->baseFont ? it->second->baseFont.get() : nullptr;
        const bool drop = std::find(bases.begin(), bases.end(), raw) != bases.end()
                          || (base && std::find(bases.begin(), bases.end(), base) != bases.end());
        if (!drop) {
            ++it;
            continue;
        }
        _pendingGlyphs.erase(raw);
        it = _fontCache.erase(it);
    }
    for (const FVictim& victim : victims) {
        _baseFontCache.erase(victim.key);
        auto sizesIt = _baseSizes.find(victim.family);
        if (sizesIt != _baseSizes.end()) {
            std::erase(sizesIt->second, rasterPx);
        }
    }
    face.bank->releaseRasterSize(rasterPx);
    for (const std::shared_ptr<FontAtlasBank>& fallback : fallbackBanks) {
        if (rasterSizeStillNeeded(fallback.get(), rasterPx)) {
            continue;
        }
        fallback->releaseRasterSize(rasterPx);
        if (FSharedFaceAtlas* fallbackFace = findSharedFace(fallback.get())) {
            std::erase(fallbackFace->lru, rasterPx);
        }
    }
    if (!victims.empty()) {
        bumpResourceRevision();
    }
    return true;
}

void FontManager::noteLiveRasterSize(const std::shared_ptr<FontAtlasBank>& bank, uint32_t rasterPx)
{
    if (!bank || rasterPx == 0) {
        return;
    }
    FSharedFaceAtlas* face = findSharedFace(bank.get());
    if (!face) {
        return;
    }
    std::erase(face->lru, rasterPx);
    face->lru.push_back(rasterPx);
    size_t spins = 0;
    const size_t limit = face->lru.size();
    while (face->lru.size() > kLiveRasterSizeWindow && spins < limit) {
        const uint32_t victim = face->lru.front();
        if (!evictRasterSize(*face, victim)) {
            face->lru.erase(face->lru.begin());
            face->lru.push_back(victim);
            ++spins;
            continue;
        }
        std::erase(face->lru, victim);
        spins = 0;
    }
    dropUnreferencedSharedFaces();
}

void FontManager::rememberSdfView(const FName& fontName, uint32_t viewPx)
{
    auto& lru = _sdfViewLru[fontName];
    std::erase(lru, viewPx);
    lru.push_back(viewPx);
    while (lru.size() > kMaxSdfViewsPerFamily) {
        bool bEvicted = false;
        for (auto it = lru.begin(); it != lru.end(); ++it) {
            if (*it == viewPx) {
                continue;
            }
            const std::string key = makeCacheKey(fontName, *it);
            auto              cacheIt = _fontCache.find(key);
            if (cacheIt != _fontCache.end() && cacheIt->second && cacheIt->second.use_count() > 1) {
                continue;
            }
            // Drop the view only. The shared SDF base stays.
            if (cacheIt != _fontCache.end() && cacheIt->second && cacheIt->second->isView()) {
                _pendingGlyphs.erase(cacheIt->second.get());
                _fontCache.erase(cacheIt);
            }
            lru.erase(it);
            bEvicted = true;
            break;
        }
        if (!bEvicted) {
            break;
        }
    }
}

std::shared_ptr<Font> FontManager::getFont(const FName &fontName, uint32_t fontSize, std::optional<float> dpiScale)
{
    (void)dpiScale;
    if (fontSize == 0) {
        fontSize = 1;
    }
    // The requested size IS the device pixel size. Zoom and DPI are folded
    // by planTextRaster before the call, so the key is the integer size.
    const std::string key = makeCacheKey(fontName, fontSize);
    if (auto it = _fontCache.find(key); it != _fontCache.end()) {
        std::shared_ptr<Font> font = it->second;
        if (font && font->atlas) {
            const uint32_t rasterPx = (font->isView() && font->baseFont)
                                          ? static_cast<uint32_t>(std::lround(font->baseFont->fontSize))
                                          : fontSize;
            noteLiveRasterSize(font->atlas, rasterPx);
        }
        return font;
    }

    // A hinted bitmap is pixel-aligned. Serving 16px from a 13px atlas
    // stretches by 16/13 and nearest sampling drops stroke texels. Only an
    // exact-size bitmap atlas may serve the request. An unpinned
    // pre-registered face has no file to rasterize, so a scaled view is the
    // headless stand-in.
    if (chooseModeForSize(fontSize) == EFontRenderMode::Bitmap) {
        if (auto base = findBestBase(fontName, fontSize); base && !base->bTexelPinned) {
            auto view = makeScaledView(base, fontSize);
            _fontCache[key] = view;
            return view;
        }
        auto pathIt = _fontPaths.find(fontName);
        if (_render && pathIt != _fontPaths.end()) {
            YA_CORE_INFO("FontManager: lazily building bitmap base '{}' size {} (exact device raster)",
                         fontName.toString(), fontSize);
            return loadFont(*_render, pathIt->second, fontName, fontSize);
        }
        YA_CORE_WARN("Font '{}' not in cache. Call loadFont first.", fontName.toString());
        return nullptr;
    }

    // SDF is scale-free. The closest base serves a view whose metrics are
    // already the requested device pixels; makeText then draws at scale 1.
    if (auto base = findBestBase(fontName, fontSize)) {
        if (static_cast<uint32_t>(std::lround(base->fontSize)) == fontSize) {
            _fontCache[key] = base;
            return base;
        }
        auto view = makeScaledView(base, fontSize);
        _fontCache[key] = view;
        rememberSdfView(fontName, fontSize);
        return view;
    }

    auto pathIt = _fontPaths.find(fontName);
    if (_render && pathIt != _fontPaths.end()) {
        YA_CORE_INFO("FontManager: lazily building '{}' size {} (no preloaded base in that flavor)",
                     fontName.toString(), fontSize);
        return loadFont(*_render, pathIt->second, fontName, fontSize);
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
    _fallbackDefs.clear();
    _sdfViewLru.clear();
    _sharedFaces.clear();
    // _render is a non-owning observer; a fresh loadFont will re-capture it.
    bumpResourceRevision();
    YA_CORE_INFO("Cleared all font cache");
}

std::shared_ptr<Font> FontManager::loadFont(IRender& render, const std::string &fontPath, const FName &fontName, uint32_t fontSize,
                                            std::optional<EFontRenderMode> mode, std::optional<float> dpiScale)
{
    YA_PROFILE_FUNCTION_LOG();
    // `fontSize` is the device pixel size. dpiScale is not part of the key.
    (void)dpiScale;
    if (fontSize == 0) {
        fontSize = 1;
    }
    // Capture the render handle so getFont() can lazily build a base in the
    // correct flavor when none is preloaded (font-framework plan §1).
    _render = &render;
    _fontPaths[fontName] = fontPath;

    const std::string requestKey = makeCacheKey(fontName, fontSize);
    if (auto it = _fontCache.find(requestKey); it != _fontCache.end()) {
        return it->second;
    }

    // Size-driven flavor split (font-framework plan §1): small glyphs use the
    // hinted grayscale bitmap (crisp, no SDF bite-out); larger glyphs use the
    // SDF distance field. An explicit mode overrides the auto split.
    const EFontRenderMode chosenMode = mode.value_or(chooseModeForSize(fontSize));

    // Bitmap is rasterized at the requested device pixel size and returned as
    // that atlas (no scaled view). SDF keeps one base at max(kSdfBaseSize,
    // request) and serves other sizes as views whose metrics are already the
    // requested pixel size.
    const uint32_t rasterSize = (chosenMode == EFontRenderMode::SDF)
                                    ? std::max(kSdfBaseSize, fontSize)
                                    : fontSize;
    const std::string baseKey = makeCacheKey(fontName, rasterSize);
    if (auto it = _baseFontCache.find(baseKey); it != _baseFontCache.end()) {
        if (chosenMode == EFontRenderMode::Bitmap || rasterSize == fontSize) {
            return it->second;
        }
        auto view = makeScaledView(it->second, fontSize);
        _fontCache[requestKey] = view;
        rememberSdfView(fontName, fontSize);
        return view;
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

    FT_Set_Pixel_Sizes(face, 0, rasterSize);

    auto font        = std::make_shared<Font>();
    font->fontSize   = (float)rasterSize;
    font->fontPath   = fontPath;
    font->family     = fontName;
    font->lineHeight = (float)(face->size->metrics.height >> 6);    // 26.6 fixed point to integer
    font->ascent     = (float)(face->size->metrics.ascender >> 6);  // Distance from baseline to top
    font->descent    = (float)(face->size->metrics.descender >> 6); // Distance from baseline to bottom (negative)
    // SDF is scale-free (drawn at the mapping scale); only bitmap bases are
    // pinned to the density they were rasterized at.
    font->bTexelPinned = (chosenMode == EFontRenderMode::Bitmap);

    // One bank per (face file, flavor). This size is a light font: metrics
    // and a character map whose slots point into that shared bank.
    FSharedFaceAtlas& shared = sharedFace(render, fontPath, chosenMode, "primary");
    font->rasterizer = shared.rasterizer;
    font->renderMode = chosenMode;
    font->atlas      = shared.bank;

    for (uint32_t codePoint : BASE_GLYPH_CODEPOINTS) {
        GlyphBitmap glyph = font->rasterizer->rasterize(face, codePoint, rasterSize);
        Character   character;
        character.size       = {static_cast<int>(glyph.width), static_cast<int>(glyph.height)};
        character.bearing    = glyph.bearing;
        character.advance    = glyph.advance;
        character.designSize = rasterSize;
        if (glyph.width > 0 && glyph.height > 0) {
            uint32_t slot = 0;
            glm::vec4 uv{};
            if (!font->atlas->findGlyph(codePoint, rasterSize, slot, uv)) {
                slot = font->atlas->addGlyph(glyph.width, glyph.height, glyph.pixels.data(), codePoint, rasterSize);
                if (slot != ~0u) {
                    uv = font->atlas->getUv(slot);
                }
            }
            character.atlasSlot = slot;
            character.uvRect    = uv;
        }
        else {
            character.atlasSlot = ~0u;
        }
        font->characters[codePoint] = character;
    }

    // Growth during the loop keeps slot indices and moves UVs. Re-read them
    // for the glyphs packed before the grow; this font is not cached yet, so
    // the shared onRepack did not see it.
    for (auto& [codePoint, character] : font->characters) {
        (void)codePoint;
        if (character.atlasIndex == 0 && character.atlasSlot != ~0u) {
            character.uvRect = font->atlas->getUv(character.atlasSlot);
        }
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Upload at a safe point (font load is outside command recording).
    font->atlas->upload();
    font->atlasTexture = font->atlas->pageCount() ? font->atlas->pageTexture(0) : nullptr;
    if (_fontAtlasTextureSink) {
        _fontAtlasTextureSink(fontName, rasterSize, font->atlasTexture);
    }

    // Cache the base font and its exact-size fast path.
    _baseFontCache[baseKey] = font;
    _fontCache[baseKey] = font;
    auto& sizes = _baseSizes[fontName];
    if (std::find(sizes.begin(), sizes.end(), rasterSize) == sizes.end()) {
        sizes.push_back(rasterSize);
    }
    bumpResourceRevision();

    YA_CORE_INFO("Loaded font '{}' (mode={}, rasterSize: {}, shared pages: {})",
                 fontName.toString(),
                 (int)chosenMode,
                 rasterSize,
                 font->atlas->pageCount());

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

    // Bitmap atlases are the draw font: fallback faces on a bitmap base are
    // rasterized at the same pixel size (attachFallbackToBase), so there is
    // no view to rescale them. SDF requests that differ from the base size
    // get a view whose metrics are already `fontSize` device pixels.
    noteLiveRasterSize(font->atlas, rasterSize);
    if (chosenMode == EFontRenderMode::Bitmap || rasterSize == fontSize) {
        return font;
    }
    auto view = makeScaledView(font, fontSize);
    _fontCache[makeCacheKey(fontName, fontSize)] = view;
    rememberSdfView(fontName, fontSize);
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
    // Shared with every other size of this fallback face. The page appears
    // when the first glyph is captured, not here.
    FSharedFaceAtlas& shared = sharedFace(render, def.path, effectiveMode, "fallback");
    entry.rasterizer   = shared.rasterizer;
    entry.atlas        = shared.bank;
    entry.atlasTexture = entry.atlas->pageCount() ? entry.atlas->pageTexture(0) : nullptr;
    font.fallbacks.push_back(std::move(entry));
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
    // idempotent per (name, raster px), so reloading the same name would hand
    // back the previously rasterized face. Drop every registration this name
    // owns - cached views, bases, its size list, its fallback defs and the
    // recorded path - then load. Dropping the cached VIEWS matters as much as
    // the bases: they hold shared_ptr to the old bases, so a surviving view
    // keeps the previous face alive and getFont would keep serving it.
    const FName primaryName(DEFAULT_RUNTIME_FONT_NAME);
    const std::string prefix = primaryName.toString() + ":";
    for (auto it = _fontCache.begin(); it != _fontCache.end();) {
        if (!it->first.starts_with(prefix)) {
            ++it;
            continue;
        }
        if (it->second) {
            _pendingGlyphs.erase(it->second.get());
        }
        it = _fontCache.erase(it);
    }
    for (auto it = _baseFontCache.begin(); it != _baseFontCache.end();) {
        if (!it->first.starts_with(prefix)) {
            ++it;
            continue;
        }
        if (it->second) {
            _pendingGlyphs.erase(it->second.get());
        }
        it = _baseFontCache.erase(it);
    }
    _baseSizes.erase(primaryName);
    _fontPaths.erase(primaryName);
    _fallbackDefs.erase(primaryName);
    _sdfViewLru.erase(primaryName);
    dropUnreferencedSharedFaces();

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
                uint32_t slot = 0;
                glm::vec4 uv{};
                if (!atlas.findGlyph(codePoint, pixelSize, slot, uv)) {
                    slot = atlas.addGlyph(glyph.width, glyph.height, glyph.pixels.data(), codePoint, pixelSize);
                    if (slot == ~0u) {
                        return false; // atlas could not pack it (hit size ceiling) — let another face try
                    }
                    uv = atlas.getUv(slot);
                }
                character.atlasSlot = slot;
                character.uvRect    = uv;
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
    for (const auto& [key, face] : _sharedFaces) {
        (void)key;
        if (!face.bank || face.bank->pageCount() == 0) {
            continue;
        }
        const std::string faceName = atlasDebugFaceName(face.path, face.role);
        const uint32_t pageCount = static_cast<uint32_t>(face.bank->pageCount());
        for (uint32_t i = 0; i < pageCount; ++i) {
            const DynamicFontAtlas* atlas = face.bank->pageAtlas(i);
            if (!atlas) {
                continue;
            }
            FFontAtlasDebugPage page;
            page.texture     = face.bank->pageTexture(i);
            page.pageIndex   = i;
            page.pageCount   = pageCount;
            page.renderMode  = face.mode;
            page.glyphCount  = atlas->liveGlyphCount();
            page.cpuBytes    = atlas->allocatedBytes();
            page.rasterSizes = atlas->rasterSizes();
            const uint32_t w = page.texture ? page.texture->getWidth() : atlas->size();
            const uint32_t h = page.texture ? page.texture->getHeight() : atlas->size();
            page.label = std::format("{}  {}  {}  {}/{}",
                                     faceName,
                                     fontRenderModeName(face.mode),
                                     face.role,
                                     i + 1,
                                     pageCount);
            std::string sizes;
            for (uint32_t rasterPx : page.rasterSizes) {
                if (!sizes.empty()) {
                    sizes += ' ';
                }
                sizes += std::to_string(rasterPx);
            }
            if (sizes.empty()) {
                sizes = "none";
            }
            page.detail = std::format("{} glyphs  ·  {}×{}  ·  sizes {}  ·  1:1",
                                      page.glyphCount,
                                      w,
                                      h,
                                      sizes);
            if (!face.path.empty()) {
                page.detail += "  ·  ";
                page.detail += face.path;
            }
            pages.push_back(std::move(page));
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
