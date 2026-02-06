#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Resource/ResourceRegistry.h"
#include "Render/Core/Texture.h"

namespace ya
{


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
    std::unordered_map<char, Character> characters;
    float                               fontSize   = 0;
    float                               lineHeight = 0;         // Line height (ascender - descender + line gap)
    float                               ascent     = 0;         // Distance from baseline to top of tallest glyph
    float                               descent    = 0;         // Distance from baseline to bottom of lowest glyph
    std::string                         fontPath;               // Path to font file
    std::shared_ptr<Texture>            atlasTexture = nullptr; // Single texture atlas (optional)

    bool hasCharacter(char asciiCode) const { return characters.contains(asciiCode); }
    bool hasCharacter(wchar_t wideChar) const
    {
        // Currently only supports ASCII characters
        return wideChar < 128 && hasCharacter(static_cast<char>(wideChar));
    }

    float            getFontSize() const { return fontSize; }
    const Character &getCharacter(char c) const
    {
        // TODO: draw 口 for missing character
        static Character defaultChar{};
        auto             it = characters.find(c);
        if (it != characters.end()) {
            return it->second;
        }
        return defaultChar;
    }

    // Measure text width for layout calculations
    float measureText(const std::string &text) const
    {
        float width = 0.0f;
        for (char c : text) {
            const Character &ch = getCharacter(c);
            width += ch.advance.x;
        }
        return width;
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

    // TODO: optimize key generation
    static std::string makeCacheKey(const FName &fontName, uint32_t fontSize)
    {
        return std::format("{}:{}", fontName.toString(), fontSize);
    }
};

} // namespace ya