#include "Render2D.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"

#include "Resource/AssetManager.h"

#include "utility.cc/ranges.h"

#include <limits>

namespace ya
{

FRender2dData Render2D::data;
FQuadRender*  Render2D::quadData = nullptr;

auto& data2D = Render2D::data;

void Render2D::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    quadData = new FQuadRender();
    quadData->init(render, colorFormat, depthFormat);
}

void Render2D::destroy()
{
    quadData->destroy();
    delete quadData;
}

void Render2D::onUpdate(float dt)
{
    (void)dt;
}

void Render2D::onRender()
{
}

void Render2D::begin(const FRender2dContext& ctx)
{
    data.curCmdBuf    = ctx.cmdBuf;
    data.cam          = ctx.cam;
    data.windowHeight = ctx.windowHeight;
    data.windowWidth  = ctx.windowWidth;
    Extent2D extent{.width = data.windowWidth, .height = data.windowHeight};
    quadData->begin(extent);
}

void FQuadRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 4,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 2,
                },
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 32,
                },
            },
        });

    _frameUboDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[0]);
    std::vector<ya::DescriptorSetHandle> descriptorSets;
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, 1, descriptorSets);
    _frameUboDS     = descriptorSets[0];
    _frameUBOBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_Screen_FrameUBO",
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, 1, descriptorSets);
    _worldFrameUboDS     = descriptorSets[0];
    _worldFrameUBOBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_World_FrameUBO",
            .usage       = EBufferUsage::UniformBuffer,
            .size        = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    _resourceDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[1]);
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(_resourceDSL, 2, descriptorSets);
    _resourceDS      = descriptorSets[0];
    _worldResourceDS = descriptorSets[1];

    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _resourceDSL};
    _pipelineLayout = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

    const auto buildVertexAttributes = []()
    {
        return std::vector<VertexAttribute>{
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 0,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(Vertex, pos),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 1,
                .format     = EVertexAttributeFormat::Float4,
                .offset     = offsetof(Vertex, color),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 2,
                .format     = EVertexAttributeFormat::Float2,
                .offset     = offsetof(Vertex, texCoord),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 3,
                .format     = EVertexAttributeFormat::Uint,
                .offset     = offsetof(Vertex, textureIdx),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 4,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(Vertex, worldCenter),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 5,
                .format     = EVertexAttributeFormat::Float3,
                .offset     = offsetof(Vertex, worldDirection),
            },
            VertexAttribute{
                .bufferSlot = 0,
                .location   = 6,
                .format     = EVertexAttributeFormat::Float2,
                .offset     = offsetof(Vertex, worldSize),
            },
        };
    };

    const auto buildViewportState = []()
    {
        return ViewportState{
            .viewports = {Viewport{
                .x        = 0.0f,
                .y        = 0.0f,
                .width    = static_cast<float>(Render2D::data.windowWidth),
                .height   = static_cast<float>(Render2D::data.windowHeight),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            }},
            .scissors  = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = Render2D::data.windowWidth,
                .height  = Render2D::data.windowHeight,
            }},
        };
    };

    _pipeline = IGraphicsPipeline::create(render);
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang", .entryName = "vertMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            .vertexAttributes = buildVertexAttributes(),
            .defines          = {
                std::format("TEXTURE_SET_SIZE {}", TEXTURE_SET_SIZE),
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
            EPipelineDynamicFeature::CullMode,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildViewportState(),
    });

    _worldPipeline = IGraphicsPipeline::create(render);
    _worldPipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef            = 0,
        .renderPass            = nullptr,
        .pipelineRenderingInfo = PipelineRenderingInfo{
            .label                  = "Sprite2D_World_Pipeline",
            .viewMask               = 0,
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat  = depthFormat,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc = ShaderDesc{
            .sourceMode        = ShaderDesc::ESourceMode::StageFiles,
            .stageFiles        = {
                ShaderDesc::StageFile{.stage = EShaderStage::Vertex, .file = "Sprite2D.slang", .entryName = "vertWorldMain"},
                ShaderDesc::StageFile{.stage = EShaderStage::Fragment, .file = "Sprite2D.slang", .entryName = "fragMain"},
            },
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            .vertexAttributes = buildVertexAttributes(),
            .defines          = {
                std::format("TEXTURE_SET_SIZE {}", TEXTURE_SET_SIZE),
            },
        },
        .dynamicFeatures = {
            EPipelineDynamicFeature::Viewport,
            EPipelineDynamicFeature::Scissor,
            EPipelineDynamicFeature::CullMode,
        },
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .cullMode    = ECullMode::Back,
            .frontFace   = EFrontFaceType::CounterClockWise,
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = false,
            .bDepthWriteEnable      = false,
            .depthCompareOp         = ECompareOp::Always,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .bLogicOpEnable = false,
            .attachments    = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = true,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = static_cast<EColorComponent::T>(EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A),
                },
            },
        },
        .viewportState = buildViewportState(),
    });

    _vertexBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_Screen_VertexBuffer",
            .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    vertexPtr     = _vertexBuffer->map<FQuadRender::Vertex>();
    vertexPtrHead = vertexPtr;

    _worldVertexBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_World_VertexBuffer",
            .usage       = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size        = sizeof(FQuadRender::Vertex) * MaxVertexCount,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
    worldVertexPtr     = _worldVertexBuffer->map<FQuadRender::Vertex>();
    worldVertexPtrHead = worldVertexPtr;

    std::vector<uint32_t> indices(MaxIndexCount);
    for (uint32_t i = 0; i < MaxIndexCount; i += 6) {
        const uint32_t vertexIndex = (i / 6) * 4;
        indices[i + 0] = vertexIndex + 0;
        indices[i + 1] = vertexIndex + 1;
        indices[i + 2] = vertexIndex + 3;
        indices[i + 3] = vertexIndex + 0;
        indices[i + 4] = vertexIndex + 3;
        indices[i + 5] = vertexIndex + 2;
    }

    _indexBuffer = render->getResourceFactory()->createBuffer(
        ya::BufferCreateInfo{
            .label       = "Sprite2D_IndexBuffer",
            .usage       = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .data        = indices.data(),
            .size        = sizeof(uint32_t) * MaxIndexCount,
            .memoryUsage = EMemoryUsage::GpuOnly,
        });
}

void FQuadRender::destroy()
{
    _vertexBuffer.reset();
    _worldVertexBuffer.reset();
    _indexBuffer.reset();

    _frameUBOBuffer.reset();
    _worldFrameUBOBuffer.reset();
    _frameUboDSL.reset();

    _descriptorPool.reset();
    _pipeline.reset();
    _worldPipeline.reset();
    _pipelineLayout.reset();
}

void FQuadRender::begin(const Extent2D& extent)
{
    _textureBindings.clear();
    _textureLabel2Idx.clear();
    _textureBindings.push_back(TextureBinding{
        .texture = TextureLibrary::get().getWhiteTexture(),
        .sampler = TextureLibrary::get().getDefaultSampler(),
    });

    float w      = static_cast<float>(extent.width);
    float h      = static_cast<float>(extent.height);
    float aspect = w / h;
    if (w > h) {
        h = w / aspect;
    }
    else {
        w = h * aspect;
    }

    _screenOrthoProj = glm::orthoRH_ZO(0.0f, w, 0.0f, h, -1.0f, 1.0f);
}

void FQuadRender::end()
{
    flushWorld(Render2D::data.curCmdBuf);
    flush(Render2D::data.curCmdBuf);
}

void FQuadRender::flush(ICommandBuffer* cmdBuf)
{
    if (!cmdBuf || vertexCount == 0) {
        return;
    }

    updateResources(_resourceDS);
    updateFrameUBO(_frameUBOBuffer, _frameUboDS, _screenOrthoProj, glm::mat4(1.0f));
    _vertexBuffer->flush();

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f,
                        0.0f,
                        static_cast<float>(Render2D::data.windowWidth),
                        static_cast<float>(Render2D::data.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setScissor(0, 0, Render2D::data.windowWidth, Render2D::data.windowHeight);
    cmdBuf->setCullMode(data2D.screenCullMode);

    std::vector<DescriptorSetHandle> descriptorSets = {_frameUboDS, _resourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    indexCount  = 0;
}

void FQuadRender::flushWorld(ICommandBuffer* cmdBuf)
{
    if (!cmdBuf || worldVertexCount == 0) {
        return;
    }

    // YA_CORE_INFO("World overlay flush: {} vertices ({} quads), {} indices",
    //                worldVertexCount, worldVertexCount / 4, worldIndexCount);

    updateResources(_worldResourceDS);
    updateFrameUBO(_worldFrameUBOBuffer, _worldFrameUboDS, data2D.cam.viewProjection, data2D.cam.view);
    _worldVertexBuffer->flush();

    cmdBuf->bindPipeline(_worldPipeline.get());
    cmdBuf->setViewport(0.0f,
                        static_cast<float>(Render2D::data.windowHeight),
                        static_cast<float>(Render2D::data.windowWidth),
                        -static_cast<float>(Render2D::data.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setCullMode(Render2D::data.worldCullMode);
    cmdBuf->setScissor(0, 0, Render2D::data.windowWidth, Render2D::data.windowHeight);

    std::vector<DescriptorSetHandle> descriptorSets = {_worldFrameUboDS, _worldResourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);
    cmdBuf->bindVertexBuffer(0, _worldVertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);
    cmdBuf->drawIndexed(static_cast<uint32_t>(worldIndexCount), 1, 0, 0, 0);

    worldVertexPtr   = worldVertexPtrHead;
    worldVertexCount = 0;
    worldIndexCount  = 0;
}

void FQuadRender::updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer,
                                 DescriptorSetHandle       dsHandle,
                                 const glm::mat4&          viewProj,
                                 const glm::mat4&          view)
{
    FrameUBO ubo{
        .viewProj = viewProj,
        .view     = view,
    };
    uboBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(uboBuffer->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(dsHandle,
                                                 0,
                                                 0,
                                                 EPipelineDescriptorType::UniformBuffer,
                                                 {bufferInfo}),
        },
        {});
}

void FQuadRender::updateResources(DescriptorSetHandle dsHandle)
{
    std::vector<DescriptorImageInfo> imageInfos;

    auto defaultSampler = TextureLibrary::get().getDefaultSampler();
    auto whiteTexture   = TextureLibrary::get().getWhiteTexture();

    for (uint32_t i = imageInfos.size(); i < TEXTURE_SET_SIZE; i++) {
        if (i < _textureBindings.size()) {
            auto& tb = _textureBindings[i];
            imageInfos.emplace_back(tb.getImageViewHandle(), tb.getSamplerHandle(), EImageLayout::ShaderReadOnlyOptimal);
        }
        else {
            imageInfos.emplace_back(whiteTexture->getImageView()->getHandle(),
                                    defaultSampler->getHandle(),
                                    EImageLayout::ShaderReadOnlyOptimal);
        }
    }

    _render->getDescriptorHelper()->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(dsHandle,
                                                0,
                                                0,
                                                EPipelineDescriptorType::CombinedImageSampler,
                                                imageInfos),
        },
        {});
}

void FQuadRender::drawTextureInternal(const glm::mat4& transform,
                                      uint32_t         textureIdx,
                                      const glm::vec3  tint,
                                      const glm::vec2& uvScale,
                                      const glm::vec2& uvTranslation)
{
    for (int i = 0; i < 4; i++) {
        *vertexPtr = FQuadRender::Vertex{
            .pos         = transform * FQuadRender::vertices[i],
            .color       = {tint, 1.0f},
            .texCoord    = FQuadRender::defaultTexcoord[i] * uvScale + uvTranslation,
            .textureIdx  = textureIdx,
            .worldCenter = glm::vec3(0.0f),
            .worldDirection = glm::vec3(0.0f, 0.0f, -1.0f),
            .worldSize   = glm::vec2(0.0f),
        };
        ++vertexPtr;
    }

    vertexCount += 4;
    indexCount += 6;
}

void FQuadRender::drawWorldTextureInternal(const glm::vec3&            center,
                                           const glm::vec3&            direction,
                                           const glm::vec2&            size,
                                           uint32_t                    textureIdx,
                                           const glm::vec3             tint,
                                           const glm::vec2&            uvScale)
{
    const glm::vec3 normalizedDirection = glm::length2(direction) > std::numeric_limits<float>::epsilon()
                                            ? glm::normalize(direction)
                                            : glm::vec3(0.0f, -1.0f, 0.0f);

    for (int i = 0; i < 4; i++) {
        *worldVertexPtr = FQuadRender::Vertex{
            .pos         = glm::vec3(FQuadRender::vertices[i]),
            .color       = {tint, 1.0f},
            .texCoord    = FQuadRender::defaultTexcoord[i] * uvScale,
            .textureIdx  = textureIdx,
            .worldCenter = center,
            .worldDirection = normalizedDirection,
            .worldSize   = size,
        };
        ++worldVertexPtr;
    }

    worldVertexCount += 4;
    worldIndexCount += 6;
}

void FQuadRender::drawTexture(const glm::vec3& position,
                              const glm::vec2& size,
                              ya::Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale)
{
    if (shouldFlush()) {
        flush(Render2D::data.curCmdBuf);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    uint32_t textureIdx = findOrAddTexture(texture);
    drawTextureInternal(model, textureIdx, tint, uvScale);
}

void FQuadRender::drawTexture(const glm::mat4& transform,
                              ya::Ptr<Texture> texture,
                              const glm::vec4& tint,
                              const glm::vec2& uvScale)
{
    if (shouldFlush()) {
        flush(Render2D::data.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);
    drawTextureInternal(transform, textureIdx, tint, {uvScale.x, uvScale.y});
}

void FQuadRender::drawWorldTexture(const glm::vec3&            center,
                                   const glm::vec3&            direction,
                                   const glm::vec2&            size,
                                   ya::Ptr<Texture>            texture,
                                   const glm::vec4&            tint,
                                   const glm::vec2&            uvScale)
{
    if (shouldFlushWorld()) {
        flushWorld(Render2D::data.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);
    drawWorldTextureInternal(center, direction, size, textureIdx, tint, {uvScale.x, uvScale.y});
}

void FQuadRender::drawSubTexture(const glm::vec3& position,
                                 const glm::vec2& size,
                                 ya::Ptr<Texture> texture,
                                 const glm::vec4& tint,
                                 const glm::vec4& uvRect)
{
    if (shouldFlush()) {
        flush(Render2D::data.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    drawTextureInternal(model, textureIdx, tint, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y});
}

void FQuadRender::drawText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font)
{
    float cursorX = position.x;
    float cursorY = position.y;

    YA_CORE_ASSERT(font != nullptr, "TODO: font is null in Render2D::drawText, should make a default font");
    FontManager::get()->ensureGlyphs(*font, text);

    const auto codePoints = utf8::decode(text);
    for (uint32_t codePoint : codePoints) {
        if (codePoint == '\r') {
            continue;
        }
        if (codePoint == '\n') {
            cursorX = position.x;
            cursorY += font->lineHeight;
            continue;
        }

        const Character& character = font->getCharacter(codePoint);
        if (codePoint == ' ') {
            cursorX += character.advance.x;
            continue;
        }
        if (codePoint == '\t') {
            cursorX += font->getCharacter(' ').advance.x * 4.0f;
            continue;
        }

        float xpos = cursorX + static_cast<float>(character.bearing.x);
        float ypos = cursorY + static_cast<float>(font->ascent - character.bearing.y);
        glm::vec3 pos  = glm::vec3(xpos, ypos, position.z);

        if (!character.bInAtlas) {
            if (character.standaloneTexture) {
                drawTexture(pos, glm::vec2(character.size), character.standaloneTexture, color);
            }
        }
        else {
            drawSubTexture(pos,
                           glm::vec2(character.size),
                           font->atlasTexture,
                           color,
                           character.uvRect);
        }

        cursorX += character.advance.x;
    }
}

} // namespace ya
