#include "Render2D.h"

#include "Core/MessageBus.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderPass.h"
#include "Render/Core/Swapchain.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "imgui.h"

#include "Core/AssetManager.h"

#include "utility.cc/ranges.h"

#define DYN_CULL 1

namespace ya
{

FRender2dData Render2D::data;
FQuadRender  *Render2D::quadData = nullptr;

auto &data2D = Render2D::data;


void Render2D::init(IRender *render, IRenderPass *renderpass)
{
    auto *swapchain = render->getSwapchain();
    auto  extent    = swapchain->getExtent();

    data.windowWidth  = extent.width;
    data.windowHeight = extent.height;

    MessageBus::get()->subscribe<WindowResizeEvent>([render](const WindowResizeEvent &ev) {
        auto *swapchain = render->getSwapchain();
        auto  extent    = swapchain->getExtent();
        YA_CORE_INFO("Window resized, swapchain extend: {}x{}, event: {}x{}", extent.width, extent.height, ev.GetWidth(), ev.GetHeight());
        data.windowWidth  = extent.width;
        data.windowHeight = extent.height;
        return false;
    });

    quadData = new FQuadRender();
    quadData->init(render, renderpass);
}

void Render2D::destroy()
{
    quadData->destroy();
    delete quadData;
}

void Render2D::onUpdate(float dt)
{
    for (auto sys : Render2D::data._systems)
    {
        sys->onUpdate(dt);
    }
}

void Render2D::onRender()
{
    for (auto sys : Render2D::data._systems)
    {
        sys->onRender();
    }
}

void Render2D::onImGui()
{
#if DYN_CULL
    int cull = (int)(data.cullMode);
    if (ImGui::Combo("Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
        switch (cull) {
        case 0:
            data.cullMode = ECullMode::None;
            break;
        case 1:
            data.cullMode = ECullMode::Front;
            break;
        case 2:
            data.cullMode = ECullMode::Back;
            break;
        case 3:
            data.cullMode = ECullMode::FrontAndBack;
            break;
        default:
            data.cullMode = ECullMode::Back;
            break;
        }
    }
#endif

    ImGui::InputInt("Text Layout Mode", &data2D.TextLayoutMode);
    ImGui::InputInt("View Matrix Mode", &data2D.viewMatrixMode);


    quadData->onImGui();
}


// MARK: quad init

void FQuadRender::init(IRender *render, IRenderPass *renderPass)
{
    _render = render;

    _pipelineDesc = PipelineDesc{
        .pushConstants        = {},
        .descriptorSetLayouts = {
            DescriptorSetLayout{
                .label    = "Frame_UBO",
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
                .label    = "CombinedImageSampler",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = TEXTURE_SET_SIZE,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 2,
            .poolSizes = {
                DescriptorPoolSize{
                    .type            = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = 1,
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
    _frameUBOBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_FrameUBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    _resourceDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[1]);
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(_resourceDSL, 1, descriptorSets);
    _resourceDS = descriptorSets[0];


    // Use factory method to create pipeline layout
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _resourceDSL};
    _pipelineLayout                                           = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

    // Use factory method to create graphics pipeline
    _pipeline = IGraphicsPipeline::create(render, renderPass, _pipelineLayout.get());
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef = 0,
        .shaderDesc = ShaderDesc{
            .shaderName        = "Sprite2D.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot = 0,
                    // ??? no copy pasted!!! error
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            // MARK: vertex attributes
            .vertexAttributes = {
                // (layout binding = 0, set = 0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(Vertex, pos),
                },
                // aColor
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float4,
                    .offset     = offsetof(Vertex, color),
                },
                // uv
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(Vertex, texCoord),
                },
                // texture index
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 3,
                    .format     = EVertexAttributeFormat::Uint,
                    .offset     = offsetof(Vertex, textureIdx),
                },
            },
            .defines = {
                "TEXTURE_SET_SIZE " + std::to_string(TEXTURE_SET_SIZE),
            },
        },
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = static_cast<EPipelineDynamicFeature::T>(EPipelineDynamicFeature::Viewport | EPipelineDynamicFeature::Scissor
#if DYN_CULL
                                                                   | EPipelineDynamicFeature::CullMode
#endif
                                                                   ),
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
            .depthCompareOp         = ECompareOp::Less,
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
                       .bBlendEnable        = false,
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
        .viewportState = ViewportState{
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
        },
    });

    _vertexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_VertexBuffer",
            .usage         = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size          = sizeof(FQuadRender::Vertex) * MaxVertexCount,
            .memProperties = EMemoryProperty::HostVisible,
        });

    vertexPtr     = _vertexBuffer->map<FQuadRender::Vertex>();
    vertexPtrHead = vertexPtr;


    std::vector<uint32_t> indices;
    indices.resize(MaxIndexCount);
    // constant indices?
    for (uint32_t i = 0; i < MaxIndexCount; i += 6) {

        uint32_t vertexIndex = (i / 6) * 4;

        // quad-> 2 triangle-> counter-clockwise
        indices[i + 0] = vertexIndex + 0;
        indices[i + 1] = vertexIndex + 3;
        indices[i + 2] = vertexIndex + 1;

        indices[i + 3] = vertexIndex + 0; // 0;
        indices[i + 4] = vertexIndex + 2; // 2;
        indices[i + 5] = vertexIndex + 3; // 3;
    }

    _indexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_IndexBuffer",
            .usage         = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .data          = indices.data(),
            .size          = sizeof(uint32_t) * MaxIndexCount,
            .memProperties = EMemoryProperty::DeviceLocal,
        });



    // Note: White texture and default sampler are now managed by TextureLibrary
}


void FQuadRender::destroy()
{
    // Note: White texture and default sampler are now managed by TextureLibrary

    _vertexBuffer.reset();
    _indexBuffer.reset();

    _frameUBOBuffer.reset();
    _frameUboDSL.reset();

    _descriptorPool.reset();
    _pipeline.reset();
    _pipelineLayout.reset();
}

void FQuadRender::begin()
{
    _textureViews.clear();
    _textureLabel2Idx.clear();
    _textureViews.push_back(
        TextureView{
            .texture = TextureLibrary::getWhiteTexture(),
            .sampler = TextureLibrary::getDefaultSampler(),
        });


    float w      = (float)Render2D::data.windowWidth;
    float h      = (float)Render2D::data.windowHeight;
    float aspect = w / h;
    if (w > h) {
        h = w / aspect;
    }
    else {
        w = h * aspect;
    }

    /**
    LH: left-handed coordinate system
    RH: right-handed coordinate system
    NO: (-1,1) depth range (OpenGL style)
    ZO: (0,1) depth range (DirectX style)

    VK: x: right [ -1,1 ]
        y: up [ -1,1 ]
        z: backward (out to the screen) [0,1]
     */



    /*
     * In 2D rendering:
     *      map (-1,-1,0) -> (0,0,-1)
     *      map (1,1,1)   -> (w,h,1)
     * 这里的参数，按照vulkan的右手坐标:
     *      origin 在左下角，所以 bottom = 0, top = h
     *      但glm是左手坐标系，所以 near 和 far 需要反过来
     * 最终，会被映射到sd的窗口坐标系(0, 0) 在左上角:
     * x++ 向下 y++ 向右 z++ 向屏幕外
     */

    glm::mat4 proj = glm::orthoRH_ZO(0.0f,
                                     w,
                                     0.0f,
                                     h,
                                     -1.0f,
                                     1.0f);
    switch (data2D.viewMatrixMode) {
    case 1:
    {
        // center at (w/2, h/2)
        proj = glm::orthoRH_ZO(0.0f,
                               w,
                               h,
                               0.0f,
                               -1.0f,
                               1.0f);
    } break;
    }
    // flip y axis
    // proj[1][1] *= -1;

    updateFrameUBO(glm::mat4(1.0) * proj);
}

void FQuadRender::end()
{
    flush(Render2D::data.curCmdBuf);
}


// MARK: quad flush
void FQuadRender::flush(ICommandBuffer *cmdBuf)
{
    if (vertexCount <= 0) {
        return;
    }
    updateResources();

    _vertexBuffer->flush();

    // Pipeline bind using abstract command buffer interface
    cmdBuf->bindPipeline(_pipeline.get());

    // Set viewport, scissor and cull mode using abstract interface
    cmdBuf->setViewport(0.0f,
                        0.0f,
                        static_cast<float>(Render2D::data.windowWidth),
                        static_cast<float>(Render2D::data.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setScissor(0, 0, Render2D::data.windowWidth, Render2D::data.windowHeight);
#if DYN_CULL
    cmdBuf->setCullMode(Render2D::data.cullMode);
#endif

    // Bind descriptor sets using abstract interface
    std::vector<DescriptorSetHandle> descriptorSets = {_frameUboDS, _resourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);

    // Bind vertex and index buffers using abstract interface
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);

    // Draw indexed
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    indexCount  = 0;
}

void FQuadRender::updateFrameUBO(glm::mat4 viewProj)
{
    FrameUBO ubo{
        .matViewProj = viewProj,
    };
    _frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBOBuffer->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));

    auto *descriptorHelper = _render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(_frameUboDS,
                                                 0,
                                                 0,
                                                 EPipelineDescriptorType::UniformBuffer,
                                                 {bufferInfo}),
        },
        {});
}

void FQuadRender::updateResources()
{

    // TODO: let font atlas/icon atlas always on gpu not update every frame
    std::vector<DescriptorImageInfo> imageInfos;

    auto defaultSampler = TextureLibrary::getDefaultSampler();
    auto whiteTexture   = TextureLibrary::getWhiteTexture();

    // make empty slot to be updated
    for (uint32_t i = imageInfos.size(); i < TEXTURE_SET_SIZE; i++) {

        if (i < _textureViews.size()) {
            auto *tv = &_textureViews[i];
            imageInfos.emplace_back(
                tv->getSampler()->getHandle(),
                tv->getTexture()->getImageViewHandle(),
                EImageLayout::ShaderReadOnlyOptimal);
        }
        else {
            imageInfos.emplace_back(
                defaultSampler->getHandle(),
                whiteTexture->getImageViewHandle(),
                EImageLayout::ShaderReadOnlyOptimal);
        }
    }

    _render
        ->getDescriptorHelper()
        ->updateDescriptorSets(
            {
                IDescriptorSetHelper::genImageWrite(
                    _resourceDS,
                    0,
                    0,
                    EPipelineDescriptorType::CombinedImageSampler,
                    imageInfos),
            },
            {});
}



// MARK: quad  interface
void FQuadRender::drawTexture(const glm::vec3         &position,
                              const glm::vec2         &size,
                              std::shared_ptr<Texture> texture,
                              const glm::vec4         &tint,
                              const glm::vec2         &uvScale)
{
    if (shouldFlush()) {
        flush(Render2D::data.curCmdBuf);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));


    uint32_t textureIdx = findOrAddTexture(texture);

    for (int i = 0; i < 4; i++) {
        // rotation -> scale -> offset
        *vertexPtr = FQuadRender::Vertex{
            .pos        = model * FQuadRender::vertices[i],
            .color      = tint,
            .texCoord   = FQuadRender::defaultTexcoord[i] * uvScale,
            .textureIdx = textureIdx,
        };
        ++vertexPtr;
    }

    vertexCount += 4;
    indexCount += 6;
}

void FQuadRender::drawSubTexture(const glm::vec3         &position,
                                 const glm::vec2         &size,
                                 std::shared_ptr<Texture> texture,
                                 const glm::vec4         &tint,
                                 const glm::vec4         &uvRect)
{
    if (shouldFlush()) {
        flush(Render2D::data.curCmdBuf);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));
    // TODO: check texture full after findOrAddTexture, maybe use same white texture/ same sub texture
    uint32_t textureIdx = findOrAddTexture(texture);
    for (int i = 0; i < 4; i++) {
        glm::vec2 uv = FQuadRender::defaultTexcoord[i] * glm::vec2(uvRect.z, uvRect.w) + glm::vec2(uvRect.x, uvRect.y);
        *vertexPtr   = FQuadRender::Vertex{
              .pos        = model * FQuadRender::vertices[i],
              .color      = tint,
              .texCoord   = uv,
              .textureIdx = textureIdx,
        };
        ++vertexPtr;
    }

    vertexCount += 4;
    indexCount += 6;
}


void FQuadRender::drawText(const std::string &text, const glm::vec3 &position, const glm::vec4 &color, Font *font)
{
    float cursorX = position.x;
    float cursorY = position.y;

    // Debug: draw baseline (optional, comment out later)
    // drawTexture(glm::vec3(position.x, position.y, 0.01f), glm::vec2(500, 2), nullptr, glm::vec4(0, 1, 0, 1));
    YA_CORE_ASSERT(font != nullptr, "TODO: font is null in Render2D::drawText, should make a default font");

    for (size_t i = 0; i < text.size(); ++i) {
        char             ch        = text[i];
        const Character &character = font->getCharacter(ch);

        // Skip rendering for space (but still advance cursor)
        if (ch == ' ') {
            cursorX += character.advance.x;
            continue;
        }
        float xpos = cursorX;
        float ypos = cursorY;

        // Calculate glyph position
        // bearing.x: offset from cursor to left edge of glyph
        // bearing.y: offset from baseline to top of glyph (positive = above baseline)
        // position.y is the TOP of the text box, so we calculate:
        //   baseline = position.y + ascent
        //   glyph top = baseline - bearing.y
        //   result: position.y + ascent - bearing.y

        xpos += (float)(character.bearing.x);
        ypos += (float)(font->ascent - character.bearing.y);


        glm::vec3 pos = glm::vec3(xpos, ypos, position.z);

        stdptr<Texture> texture = font->atlasTexture;
        if (!character.bInAtlas) {
            UNIMPLEMENTED();
        }

        glm::vec4 uvRect = character.uvRect;
        drawSubTexture(
            pos,
            glm::vec2(character.size),
            font->atlasTexture,
            color,
            uvRect);

        // Advance cursor for next glyph (advance is in 1/64 pixels)
        cursorX += character.advance.x;
    }
}

} // namespace ya