#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Resource/ResourceRegistry.h"
#include "Render/Core/Texture.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ya
{

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
    glm::vec4                uvRect;                      // UV rect: (offsetU, offsetV, scaleU, scaleV) for drawSubTexture
    glm::ivec2               size;                        // Size of glyph in pixels
    glm::ivec2               bearing;                     // Offset from baseline to left/top of glyph
    glm::vec2                advance;                     // Horizontal offset to advance to next glyph
    std::shared_ptr<Texture> standaloneTexture = nullptr; // Individual texture for special characters
    bool                     bInAtlas          = true;    // True if character is in atlas, false if standalone
};

/**
 * @brief Font - Font atlas and glyph data
 */
struct Font
{
    std::unordered_map<uint32_t, Character> characters;
    float                                   fontSize   = 0;
    float                                   lineHeight = 0;         // Line height (ascender - descender + line gap)
    float                                   ascent     = 0;         // Distance from baseline to top of tallest glyph
    float                                   descent    = 0;         // Distance from baseline to bottom of lowest glyph
    std::string                             fontPath;               // Path to font file
    std::shared_ptr<Texture>                atlasTexture = nullptr; // Single texture atlas (optional)

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

struct FontManager : public IResourceCache
{

  private:
    // Key: "fontName:fontSize" -> Font
    std::unordered_map<std::string, stdptr<Font>> _fontCache;

  public:
    static FontManager *get();

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
    std::shared_ptr<Font> loadFont(const std::string &fontPath, const FName &fontName, uint32_t fontSize);

    std::shared_ptr<Font> getFont(const FName &fontName, uint32_t fontSize);

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
    std::shared_ptr<Font> getAdaptiveFont(const std::string &fontPath,
                                          const FName       &fontName,
                                          uint32_t           baseSize,
                                          uint32_t           windowHeight,
                                          uint32_t           referenceHeight = 1080);

    void ensureGlyphs(Font& font, std::string_view text);

    // TODO: optimize key generation
    static std::string makeCacheKey(const FName &fontName, uint32_t fontSize)
    {
        return std::format("{}:{}", fontName.toString(), fontSize);
    }
};

} // namespace ya