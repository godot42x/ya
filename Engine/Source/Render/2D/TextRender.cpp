#include "TextRender.h"

#include "Core/App/App.h"
#include "Core/Log.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "Render/TextureLibrary.h"


#include <ft2build.h>
#include FT_FREETYPE_H

namespace ya

{

void TextRender::init(IRender *render, IRenderPass *renderPass)
{
    _render = render;

    // Create pipeline layout
    PipelineDesc pipelineDesc{
        .label         = "TextRender_PipelineLayout",
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(TextPushConstant),
                .stageFlags = EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayout{
                .label    = "TextRender_FrameDSL",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex,
                    },
                },
            },
            DescriptorSetLayout{
                .label    = "TextRender_TextureDSL",
                .set      = 1,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    // Create descriptor pool
    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 130, // 1 frame UBO + 1 atlas + 128 potential standalone characters
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
                },
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 129, // 1 atlas + 128 potential standalone textures
                },
            },
        });

    // Create descriptor set layouts
    _frameUboDSL = IDescriptorSetLayout::create(render, pipelineDesc.descriptorSetLayouts[0]);
    _textureDSL  = IDescriptorSetLayout::create(render, pipelineDesc.descriptorSetLayouts[1]);

    // Allocate frame descriptor set
    std::vector<DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, 1, descriptorSets);
    _frameUboDS = descriptorSets[0];

    // Create frame UBO buffer
    _frameUBOBuffer = IBuffer::create(
        render,
        BufferCreateInfo{
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            .label         = "TextRender_FrameUBO",
        });

    // Create pipeline layout
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _textureDSL};
    _pipelineLayout                                           = IPipelineLayout::create(render, pipelineDesc.label, pipelineDesc.pushConstants, dslVec);

    // Create graphics pipeline
    _pipeline = IGraphicsPipeline::create(render, renderPass, _pipelineLayout.get());
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef = 0,
        .shaderDesc = ShaderDesc{
            .shaderName        = "Text2D.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(Vertex),
                },
            },
            .vertexAttributes = {
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float4,
                    .offset     = offsetof(Vertex, posUV),
                },
            },
        },
        .dynamicFeatures    = EPipelineDynamicFeature::Viewport | EPipelineDynamicFeature::Scissor,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable  = false,
            .bDepthWriteEnable = false,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState::defaultEnable(0),
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                Viewport{
                    .x        = 0.0f,
                    .y        = 0.0f,
                    .width    = 800.0f,
                    .height   = 600.0f,
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {
                Scissor{
                    .offsetX = 0,
                    .offsetY = 0,
                    .width   = 800,
                    .height  = 600,
                },
            },
        },
    });

    // Create vertex buffer
    _vertexBuffer = IBuffer::create(
        render,
        BufferCreateInfo{
            .usage         = EBufferUsage::VertexBuffer,
            .size          = sizeof(Vertex) * MAX_TEXT_VERTICES,
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            .label         = "TextRender_VertexBuffer",
        });

    _vertices.reserve(MAX_TEXT_VERTICES);

    YA_CORE_INFO("TextRender initialized");
}

void TextRender::destroy()
{
    _standaloneDescriptorSets.clear();
    _loadedFonts.clear();

    _vertexBuffer.reset();
    _frameUBOBuffer.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
    _frameUboDSL.reset();
    _textureDSL.reset();
    _descriptorPool.reset();

    YA_CORE_INFO("TextRender destroyed");
}

bool TextRender::loadFont(const std::string &fontPath, const FName &fontName, uint32_t fontSize)
{
    FT_Library ft{};
    if (FT_Init_FreeType(&ft)) {
        YA_CORE_ERROR("Failed to initialize FreeType library");
        return false;
    }

    FT_Face face{};
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        YA_CORE_ERROR("Failed to load font: {}", fontPath);
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);

    auto font      = std::make_shared<Font>();
    font->fontSize = fontSize;
    font->fontPath = fontPath;

    // First pass: calculate atlas dimensions
    uint32_t atlasWidth   = 0;
    uint32_t atlasHeight  = 0;
    uint32_t maxRowHeight = 0;

    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            continue;
        }
        FT_GlyphSlot &glyph = face->glyph;
        atlasWidth += glyph->bitmap.width + 1; // +1 for padding
        maxRowHeight = std::max(maxRowHeight, glyph->bitmap.rows);
    }
    atlasHeight = maxRowHeight;

    // Create atlas pixel data (RGBA)
    std::vector<ColorRGBA<uint8_t>> atlasData(static_cast<size_t>(atlasWidth) * atlasHeight, ColorRGBA<uint8_t>{0, 0, 0, 0});

    // Second pass: pack glyphs into atlas
    uint32_t offsetX = 0;
    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            YA_CORE_WARN("Failed to load glyph '{}'", c);
            continue;
        }

        FT_GlyphSlot &glyph  = face->glyph;
        FT_Bitmap    &bitmap = glyph->bitmap;

        // Copy glyph bitmap to atlas
        for (uint32_t row = 0; row < bitmap.rows; row++) {
            for (uint32_t col = 0; col < bitmap.width; col++) {
                uint8_t gray      = bitmap.buffer[row * bitmap.width + col];
                size_t  dstIdx    = (row * atlasWidth) + (offsetX + col);
                atlasData[dstIdx] = ColorRGBA<uint8_t>{255, 255, 255, gray};
            }
        }

        // Calculate UV coordinates
        float u0 = static_cast<float>(offsetX) / atlasWidth;
        float v0 = 0.0f;
        float u1 = static_cast<float>(offsetX + bitmap.width) / atlasWidth;
        float v1 = static_cast<float>(bitmap.rows) / atlasHeight;

        Character character{
            .uvRect  = glm::vec4(u0, v0, u1, v1),
            .size    = glm::ivec2(bitmap.width, bitmap.rows),
            .bearing = glm::ivec2(glyph->bitmap_left, glyph->bitmap_top),
            .advance = static_cast<uint32_t>(glyph->advance.x),
        };

        font->characters[static_cast<char>(c)] = character;
        offsetX += bitmap.width + 1; // +1 for padding
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Create atlas texture
    font->atlasTexture = std::make_shared<Texture>(atlasWidth, atlasHeight, atlasData);
    font->atlasTexture->setLabel(std::format("FontAtlas_{}", fontName.toString()));

    // Allocate descriptor set for atlas
    std::vector<DescriptorSetHandle> atlasDS;
    _descriptorPool->allocateDescriptorSets(_textureDSL, 1, atlasDS);
    _atlasDescriptorSet = atlasDS[0];

    // Update descriptor set with atlas texture
    updateTextureDS(_atlasDescriptorSet, font->atlasTexture);

    _loadedFonts[fontName] = font;
    YA_CORE_INFO("Loaded font '{}' (size: {}, atlas: {}x{})", fontName.toString(), fontSize, atlasWidth, atlasHeight);

    return true;
}

void TextRender::setFont(const FName &fontName)
{
    auto it = _loadedFonts.find(fontName);
    if (it != _loadedFonts.end()) {
        _currentFont = it->second;
    }
    else {
        YA_CORE_WARN("Font '{}' not loaded", fontName.toString());
    }
}

bool TextRender::loadSpecialCharacter(char c, uint32_t fontSize)
{
    if (!_currentFont) {
        YA_CORE_ERROR("No current font set for loading special character");
        return false;
    }

    // Check if character already exists
    if (_currentFont->hasCharacter(c)) {
        YA_CORE_WARN("Character '{}' already loaded", c);
        return true;
    }

    FT_Library ft{};
    if (FT_Init_FreeType(&ft)) {
        YA_CORE_ERROR("Failed to initialize FreeType library");
        return false;
    }

    FT_Face face{};
    if (FT_New_Face(ft, _currentFont->fontPath.c_str(), 0, &face)) {
        YA_CORE_ERROR("Failed to reload font face");
        FT_Done_FreeType(ft);
        return false;
    }

    // Use provided fontSize or current font's fontSize
    uint32_t targetFontSize = (fontSize > 0) ? fontSize : _currentFont->fontSize;
    FT_Set_Pixel_Sizes(face, 0, targetFontSize);

    if (FT_Load_Char(face, static_cast<unsigned char>(c), FT_LOAD_RENDER)) {
        YA_CORE_ERROR("Failed to load special character '{}'", c);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return false;
    }

    FT_GlyphSlot &glyph  = face->glyph;
    FT_Bitmap    &bitmap = glyph->bitmap;

    // Convert grayscale bitmap to RGBA
    std::vector<ColorRGBA<uint8_t>> pixelData;
    pixelData.reserve(static_cast<size_t>(bitmap.width) * bitmap.rows);
    for (size_t i = 0; i < static_cast<size_t>(bitmap.width) * bitmap.rows; i++) {
        uint8_t gray = bitmap.buffer[i];
        pixelData.push_back(ColorRGBA<uint8_t>{255, 255, 255, gray});
    }

    // Create standalone texture for this character
    auto standaloneTexture = std::make_shared<Texture>(bitmap.width, bitmap.rows, pixelData);
    standaloneTexture->setLabel(std::format("SpecialChar_{}", static_cast<int>(c)));

    Character character{
        .uvRect            = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), // Full texture UV
        .size              = glm::ivec2(bitmap.width, bitmap.rows),
        .bearing           = glm::ivec2(glyph->bitmap_left, glyph->bitmap_top),
        .advance           = static_cast<uint32_t>(glyph->advance.x),
        .standaloneTexture = standaloneTexture,
        .isInAtlas         = false,
    };

    _currentFont->characters[c] = character;

    // Allocate descriptor set for this standalone character
    std::vector<DescriptorSetHandle> charDS;
    _descriptorPool->allocateDescriptorSets(_textureDSL, 1, charDS);
    _standaloneDescriptorSets[c] = charDS[0];

    // Update descriptor set with texture
    updateTextureDS(charDS[0], standaloneTexture);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    YA_CORE_INFO("Loaded special character '{}' (code: {}) as standalone texture", c, static_cast<int>(c));
    return true;
}

void TextRender::begin(uint32_t screenWidth, uint32_t screenHeight)
{
    _vertices.clear();

    // Update frame UBO with orthographic projection
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(screenWidth), 0.0f, static_cast<float>(screenHeight));
    updateFrameUBO(projection);
}

void TextRender::renderText(ICommandBuffer    *cmdBuf,
                            const std::string &text,
                            const glm::vec2   &position,
                            float              scale,
                            const glm::vec3   &color)
{
    if (!_currentFont) {
        YA_CORE_WARN("No font set for text rendering");
        return;
    }

    // Bind pipeline
    _pipeline->bind(cmdBuf->getHandle());

    // Bind frame descriptor set
    std::vector<DescriptorSetHandle> descriptorSets = {_frameUboDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout->getHandle(), 0, descriptorSets);

    // Push constants for color
    TextPushConstant pc{.color = color};
    cmdBuf->pushConstants(_pipelineLayout->getHandle(), EShaderStage::Fragment, 0, sizeof(pc), &pc);

    // Separate text into atlas characters and standalone characters
    std::vector<std::pair<size_t, size_t>> atlasBatches;    // (startVertex, count)
    std::vector<std::pair<size_t, char>>   standaloneChars; // (startVertex, char)

    float  x                  = position.x;
    float  y                  = position.y;
    size_t currentVertexStart = 0;
    size_t atlasVertexCount   = 0;
    bool   inAtlasBatch       = false;

    // Generate vertices and track batches
    for (char c : text) {
        if (!_currentFont->hasCharacter(c)) {
            continue;
        }

        const Character &ch = _currentFont->characters.at(c);

        float xpos = x + static_cast<float>(ch.bearing.x) * scale;
        float ypos = y - static_cast<float>(ch.size.y - ch.bearing.y) * scale;
        float w    = static_cast<float>(ch.size.x) * scale;
        float h    = static_cast<float>(ch.size.y) * scale;

        // UV coordinates
        float u0 = ch.uvRect.x;
        float v0 = ch.uvRect.y;
        float u1 = ch.uvRect.z;
        float v1 = ch.uvRect.w;

        // 6 vertices (2 triangles) per character
        _vertices.push_back(Vertex{{xpos, ypos + h, u0, v0}}); // Bottom-left
        _vertices.push_back(Vertex{{xpos, ypos, u0, v1}});     // Top-left
        _vertices.push_back(Vertex{{xpos + w, ypos, u1, v1}}); // Top-right

        _vertices.push_back(Vertex{{xpos, ypos + h, u0, v0}});     // Bottom-left
        _vertices.push_back(Vertex{{xpos + w, ypos, u1, v1}});     // Top-right
        _vertices.push_back(Vertex{{xpos + w, ypos + h, u1, v0}}); // Bottom-right

        if (ch.isInAtlas) {
            // Atlas character
            if (!inAtlasBatch) {
                currentVertexStart = _vertices.size() - 6;
                atlasVertexCount   = 6;
                inAtlasBatch       = true;
            }
            else {
                atlasVertexCount += 6;
            }
        }
        else {
            // Standalone character - finish current atlas batch if any
            if (inAtlasBatch) {
                atlasBatches.push_back({currentVertexStart, atlasVertexCount});
                inAtlasBatch = false;
            }
            standaloneChars.push_back({_vertices.size() - 6, c});
        }

        x += static_cast<float>(ch.advance >> 6) * scale; // Advance is in 1/64 pixels
    }

    // Finish last atlas batch if any
    if (inAtlasBatch) {
        atlasBatches.push_back({currentVertexStart, atlasVertexCount});
    }

    // Upload vertices to GPU
    _vertexBuffer->writeData(_vertices.data(), _vertices.size() * sizeof(Vertex), 0);

    // Bind vertex buffer
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);

    // Render atlas batches
    if (!atlasBatches.empty()) {
        std::vector<DescriptorSetHandle> atlasDS = {_atlasDescriptorSet};
        cmdBuf->bindDescriptorSets(_pipelineLayout->getHandle(), 1, atlasDS);

        for (const auto &[startVertex, count] : atlasBatches) {
            cmdBuf->draw(static_cast<uint32_t>(count), 1, static_cast<uint32_t>(startVertex), 0);
        }
    }

    // Render standalone characters
    for (const auto &[startVertex, c] : standaloneChars) {
        auto dsIt = _standaloneDescriptorSets.find(c);
        if (dsIt != _standaloneDescriptorSets.end()) {
            std::vector<DescriptorSetHandle> charDS = {dsIt->second};
            cmdBuf->bindDescriptorSets(_pipelineLayout->getHandle(), 1, charDS);
            cmdBuf->draw(6, 1, static_cast<uint32_t>(startVertex), 0);
        }
    }
}

void TextRender::end()
{
    _vertices.clear();
}

void TextRender::updateFrameUBO(const glm::mat4 &projection)
{
    FrameUBO ubo{.projection = projection};
    _frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBOBuffer->getHandle()), 0, sizeof(FrameUBO));

    auto *descriptorHelper = _render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(
                _frameUboDS,
                0,
                0,
                EPipelineDescriptorType::UniformBuffer,
                &bufferInfo,
                1),
        },
        {});
}

void TextRender::updateTextureDS(DescriptorSetHandle ds, std::shared_ptr<Texture> texture)
{
    // DescriptorImageInfo imageInfo(
    //     TextureLibrary::getDefaultSampler()->getHandle(),
    //     texture->getImageView(),
    //     EImageLayout::ShaderReadOnlyOptimal);

    // auto *descriptorHelper = _render->getDescriptorHelper();
    // descriptorHelper->updateDescriptorSets(
    //     {
    //         IDescriptorSetHelper::genImageWrite(
    //             ds,
    //             0,
    //             0,
    //             EPipelineDescriptorType::CombinedImageSampler,
    //             &imageInfo,
    //             1),
    //     },
    //     {});
}

} // namespace ya
