#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/RenderDefines.h"


#include "glm/glm.hpp"


namespace ya
{

struct IRender;
struct IRenderPass;
struct ICommandBuffer;

/**
 * @brief Character - Glyph information
 */
struct Character
{
    glm::vec4                uvRect;          // UV coordinates in atlas (x, y, width, height)
    glm::ivec2               size;            // Size of glyph in pixels
    glm::ivec2               bearing;         // Offset from baseline to left/top of glyph
    uint32_t                 advance;         // Horizontal offset to advance to next glyph
    std::shared_ptr<Texture> standaloneTexture = nullptr; // Individual texture for special characters
    bool                     isInAtlas         = true;     // True if character is in atlas, false if standalone
};

/**
 * @brief Font - Font atlas and glyph data
 */
struct Font
{
    std::unordered_map<char, Character> characters;
    uint32_t                            fontSize     = 0;
    std::string                         fontPath;               // Path to font file
    std::shared_ptr<Texture>            atlasTexture = nullptr; // Single texture atlas (optional)

    bool hasCharacter(char c) const { return characters.find(c) != characters.end(); }
};

/**
 * @brief TextRender - Text rendering system
 *
 * Uses freetype to load fonts and render text using Text2D shader.
 *
 * Architecture:
 * - Set 0, Binding 0: Frame UBO (projection matrix)
 * - Set 1, Binding 0: Font Atlas Texture
 * - Push Constant: Text color
 * - Vertex Format: vec4 (xy: position, zw: UV)
 */
struct TextRender
{
    struct Vertex
    {
        glm::vec4 posUV; // xy: position, zw: UV
    };

    struct FrameUBO
    {
        glm::mat4 projection = glm::mat4(1.0f);
    };

    struct TextPushConstant
    {
        glm::vec3 color    = glm::vec3(1.0f);
        float     _padding = 0.0f;
    };

    IRender *_render = nullptr;

    // Font management
    std::map<FName, std::shared_ptr<Font>> _loadedFonts;
    std::shared_ptr<Font>                  _currentFont = nullptr;

    // Pipeline resources
    std::shared_ptr<IPipelineLayout>   _pipelineLayout = nullptr;
    std::shared_ptr<IGraphicsPipeline> _pipeline       = nullptr;

    // Descriptor set layouts & pools
    std::shared_ptr<IDescriptorPool>      _descriptorPool = nullptr;
    std::shared_ptr<IDescriptorSetLayout> _frameUboDSL    = nullptr;
    std::shared_ptr<IDescriptorSetLayout> _textureDSL     = nullptr;

    // Frame resources
    DescriptorSetHandle      _frameUboDS     = {};
    std::shared_ptr<IBuffer> _frameUBOBuffer = nullptr;

    // Font atlas descriptor set (shared for all characters)
    DescriptorSetHandle _atlasDescriptorSet = {};

    // Standalone character textures and descriptor sets (for special characters not in atlas)
    std::unordered_map<char, DescriptorSetHandle> _standaloneDescriptorSets;

    // Vertex buffer for dynamic text
    std::shared_ptr<IBuffer> _vertexBuffer = nullptr;
    std::vector<Vertex>      _vertices;
    static constexpr size_t  MAX_TEXT_VERTICES = 10000;

    // Public API
    void init(IRender *render, IRenderPass *renderPass);
    void destroy();

    bool loadFont(const std::string &fontPath, const FName &fontName, uint32_t fontSize);
    void setFont(const FName &fontName);
    
    // Load a special character with its own texture (not in atlas)
    bool loadSpecialCharacter(char c, uint32_t fontSize);

    void begin(uint32_t screenWidth, uint32_t screenHeight);
    void renderText(ICommandBuffer    *cmdBuf,
                    const std::string &text,
                    const glm::vec2   &position,
                    float              scale = 1.0f,
                    const glm::vec3   &color = glm::vec3{1, 1, 1});
    void end();

  private:
    void updateFrameUBO(const glm::mat4 &projection);
    void updateTextureDS(DescriptorSetHandle ds, std::shared_ptr<Texture> texture);
};

} // namespace ya