#include "Render2D.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderPass.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "imgui.h"

#include "Resource/AssetManager.h"

#include "utility.cc/ranges.h"

namespace ya
{

FRender2dData Render2D::data;
FQuadRender*  Render2D::quadData = nullptr;

auto& data2D = Render2D::data;


void Render2D::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    // windowWidth/Height are now set per-frame in begin() from the actual render target extent,
    // instead of from the swapchain. This unifies the data source.

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
    // for (auto sys : Render2D::data._systems)
    // {
    //     sys->onUpdate(dt);
    // }
}

void Render2D::onRender()
{
    // for (auto sys : Render2D::data._systems)
    // {
    //     sys->onRender();
    // }
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

void Render2D::onImGui()
{
    ImGui::Checkbox("bReverseViewPort", &data2D.bReverseViewport);

    // Debug: cull mode override for world-space pipeline (useful for inspecting front/back faces)
    int cull = (int)(data.worldCullMode);
    if (ImGui::Combo("World Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
        switch (cull) {
        case 0:
            data.worldCullMode = ECullMode::None;
            break;
        case 1:
            data.worldCullMode = ECullMode::Front;
            break;
        case 2:
            data.worldCullMode = ECullMode::Back;
            break;
        case 3:
            data.worldCullMode = ECullMode::FrontAndBack;
            break;
        default:
            data.worldCullMode = ECullMode::Front;
            break;
        }
    }
    cull = (int)data.screenCullMode;
    if (ImGui::Combo("Screen Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
        switch (cull) {
        case 0:
            data.screenCullMode = ECullMode::None;
            break;
        case 1:
            data.screenCullMode = ECullMode::Front;
            break;
        case 2:
            data.screenCullMode = ECullMode::Back;
            break;
        case 3:
            data.screenCullMode = ECullMode::FrontAndBack;
            break;
        default:
            data.screenCullMode = ECullMode::Front;
            break;
        }
    }

    ImGui::InputInt("Text Layout Mode", &data2D.TextLayoutMode);
    quadData->onImGui();
}


// MARK: quad init

void FQuadRender::init(IRender* render, EFormat::T colorFormat, EFormat::T depthFormat)
{
    _render = render;


    _descriptorPool = IDescriptorPool::create(
        render,
        DescriptorPoolCreateInfo{
            .maxSets   = 3,
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
    _frameUBOBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_Screen_FrameUBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    // World-space FrameUBO (separate buffer to avoid GPU read hazard when both paths flush in the same frame)
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(_frameUboDSL, 1, descriptorSets);
    _worldFrameUboDS     = descriptorSets[0];
    _worldFrameUBOBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_World_FrameUBO",
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    _resourceDSL = IDescriptorSetLayout::create(render, _pipelineDesc.descriptorSetLayouts[1]);
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSets(_resourceDSL, 1, descriptorSets);
    _resourceDS = descriptorSets[0];


    // Use factory method to create pipeline layout
    std::vector<std::shared_ptr<IDescriptorSetLayout>> dslVec = {_frameUboDSL, _resourceDSL};
    _pipelineLayout                                           = IPipelineLayout::create(render, "Sprite2D_PipelineLayout", _pipelineDesc.pushConstants, dslVec);

    // Use factory method to create graphics pipeline
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
            .shaderName        = "Sprite2D_Screen.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
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
                std::format("TEXTURE_SET_SIZE {}", TEXTURE_SET_SIZE),
            },
        },
        // define what state need to dynamically modified in render pass execution
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
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
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

    // MARK: world-space pipeline
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
            .shaderName        = "Sprite2D_World.glsl",
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(FQuadRender::Vertex),
                },
            },
            .vertexAttributes = {
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
            },
            .defines = {
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
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
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

    // Screen-space vertex buffer
    _vertexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_Screen_VertexBuffer",
            .usage         = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size          = sizeof(FQuadRender::Vertex) * MaxVertexCount,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    vertexPtr     = _vertexBuffer->map<FQuadRender::Vertex>();
    vertexPtrHead = vertexPtr;

    // World-space vertex buffer
    _worldVertexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_World_VertexBuffer",
            .usage         = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size          = sizeof(FQuadRender::Vertex) * MaxVertexCount,
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    worldVertexPtr     = _worldVertexBuffer->map<FQuadRender::Vertex>();
    worldVertexPtrHead = worldVertexPtr;


    std::vector<uint32_t> indices;
    indices.resize(MaxIndexCount);
    // constant indices?
    for (uint32_t i = 0; i < MaxIndexCount; i += 6) {

        uint32_t vertexIndex = (i / 6) * 4;

        // quad -> 2 triangles -> CCW when viewed from +Z (camera looks down -Z)
        // Vertices: 0=(0,0) 1=(1,0) 2=(0,1) 3=(1,1)
        // From +Z: 0→3→1 = (0,0)→(1,1)→(1,0) = CCW
        //          0→2→3 = (0,0)→(0,1)→(1,1) = CCW
        indices[i + 0] = vertexIndex + 0;
        indices[i + 1] = vertexIndex + 3;
        indices[i + 2] = vertexIndex + 1;

        indices[i + 3] = vertexIndex + 0;
        indices[i + 4] = vertexIndex + 2;
        indices[i + 5] = vertexIndex + 3;
    }

    _indexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = "Sprite2D_IndexBuffer",
            .usage         = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .data          = indices.data(),
            .size          = sizeof(uint32_t) * MaxIndexCount,
            .memoryUsage = EMemoryUsage::GpuOnly,
        });



    // Note: White texture and default sampler are now managed by TextureLibrary
}


void FQuadRender::destroy()
{
    // Note: White texture and default sampler are now managed by TextureLibrary

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
    _textureBindings.push_back(
        TextureBinding{
            .texture = TextureLibrary::get().getWhiteTexture(),
            .sampler = TextureLibrary::get().getDefaultSampler(),
        });


    float w      = (float)extent.width;
    float h      = (float)extent.height;
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

    // Screen-space ortho: (0,0) at top-left, (w,h) at bottom-right.
    // top=0, bottom=h flips Y so Y+ goes downward — standard UI convention.
    // No viewport flip needed for screen-space path.
    _screenOrthoProj = glm::orthoRH_ZO(0.0f, w, 0.0f, h, -1.0f, 1.0f);
}

void FQuadRender::end()
{
    // World-space first, then screen-space on top (UI overlays scene)
    flushWorld(Render2D::data.curCmdBuf);
    flush(Render2D::data.curCmdBuf);
}


// MARK: quad flush — screen-space
void FQuadRender::flush(ICommandBuffer* cmdBuf)
{
    if (vertexCount <= 0) {
        return;
    }
    updateResources();

    // Update FrameUBO with screen-space ortho projection
    updateFrameUBO(_frameUBOBuffer, _frameUboDS, _screenOrthoProj);

    _vertexBuffer->flush();

    // Bind screen-space pipeline
    cmdBuf->bindPipeline(_pipeline.get());

    // Screen-space always uses normal viewport (y=0, h=height) and Back cull
    cmdBuf->setViewport(0.0f,
                        0.0f,
                        static_cast<float>(Render2D::data.windowWidth),
                        static_cast<float>(Render2D::data.windowHeight),
                        0.0f,
                        1.0f);
    cmdBuf->setScissor(0, 0, Render2D::data.windowWidth, Render2D::data.windowHeight);
    cmdBuf->setCullMode(data2D.screenCullMode);

    // Bind descriptor sets
    std::vector<DescriptorSetHandle> descriptorSets = {_frameUboDS, _resourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);

    // Bind vertex and index buffers
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);

    // Draw indexed
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    indexCount  = 0;
}

// MARK: quad flush — world-space
void FQuadRender::flushWorld(ICommandBuffer* cmdBuf)
{
    if (worldVertexCount <= 0) {
        return;
    }
    updateResources();

    // Update FrameUBO with camera view-projection
    updateFrameUBO(_worldFrameUBOBuffer, _worldFrameUboDS, data2D.cam.viewProjection);

    _worldVertexBuffer->flush();

    // Bind world-space pipeline
    cmdBuf->bindPipeline(_worldPipeline.get());

    // World-space viewport & cull mode depend on bReverseViewport
    cmdBuf->setViewport(0.0f,
                        static_cast<float>(Render2D::data.windowHeight),
                        static_cast<float>(Render2D::data.windowWidth),
                        -static_cast<float>(Render2D::data.windowHeight),
                        0.0f,
                        1.0f);
    // Reversed viewport flips winding order, use configurable cull mode (default: Front to compensate)
    cmdBuf->setCullMode(Render2D::data.worldCullMode);
    cmdBuf->setScissor(0, 0, Render2D::data.windowWidth, Render2D::data.windowHeight);

    // Bind descriptor sets (world-space uses its own FrameUBO DS)
    std::vector<DescriptorSetHandle> descriptorSets = {_worldFrameUboDS, _resourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descriptorSets);

    // Bind world vertex buffer but shared index buffer
    cmdBuf->bindVertexBuffer(0, _worldVertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);

    // Draw indexed
    cmdBuf->drawIndexed(static_cast<uint32_t>(worldIndexCount), 1, 0, 0, 0);

    worldVertexPtr   = worldVertexPtrHead;
    worldVertexCount = 0;
    worldIndexCount  = 0;
}

void FQuadRender::updateFrameUBO(std::shared_ptr<IBuffer>& uboBuffer, DescriptorSetHandle dsHandle, glm::mat4 viewProj)
{
    FrameUBO ubo{
        .viewProj = viewProj,
    };
    uboBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(uboBuffer->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));

    // TODO: only update once
    auto* descriptorHelper = _render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genBufferWrite(dsHandle,
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

    auto defaultSampler = TextureLibrary::get().getDefaultSampler();
    auto whiteTexture   = TextureLibrary::get().getWhiteTexture();

    // make empty slot to be updated
    for (uint32_t i = imageInfos.size(); i < TEXTURE_SET_SIZE; i++) {

        if (i < _textureBindings.size()) {
            auto& tb = _textureBindings[i];
            imageInfos.emplace_back(
                tb.getImageViewHandle(),
                tb.getSamplerHandle(),
                EImageLayout::ShaderReadOnlyOptimal);
        }
        else {
            imageInfos.emplace_back(
                whiteTexture->getImageView()->getHandle(),
                defaultSampler->getHandle(),
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


void FQuadRender::drawTextureInternal(const glm::mat4& transform,
                                      uint32_t textureIdx, const glm::vec3 tint,
                                      const glm::vec2& uvScale,
                                      const glm::vec2& uvTranslation)
{
    for (int i = 0; i < 4; i++) {
        *vertexPtr = FQuadRender::Vertex{
            .pos        = transform * FQuadRender::vertices[i],
            .color      = {tint, 1.0f},
            .texCoord   = FQuadRender::defaultTexcoord[i] * uvScale + uvTranslation,
            .textureIdx = textureIdx,
        };
        ++vertexPtr;
    }

    vertexCount += 4;
    indexCount += 6;
}

void FQuadRender::drawWorldTextureInternal(const glm::mat4& transform,
                                           uint32_t textureIdx, const glm::vec3 tint,
                                           const glm::vec2& uvScale)
{
    for (int i = 0; i < 4; i++) {
        *worldVertexPtr = FQuadRender::Vertex{
            .pos        = transform * FQuadRender::vertices[i],
            .color      = {tint, 1.0f},
            .texCoord   = FQuadRender::defaultTexcoord[i] * uvScale,
            .textureIdx = textureIdx,
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

void FQuadRender::drawWorldTexture(const glm::mat4& transform,
                                   ya::Ptr<Texture> texture,
                                   const glm::vec4& tint,
                                   const glm::vec2& uvScale)
{
    if (shouldFlushWorld()) {
        flushWorld(Render2D::data.curCmdBuf);
    }

    uint32_t textureIdx = findOrAddTexture(texture);
    drawWorldTextureInternal(transform, textureIdx, tint, {uvScale.x, uvScale.y});
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

    // TODO: check texture full after findOrAddTexture, maybe use same white texture/ same sub texture
    uint32_t textureIdx = findOrAddTexture(texture);

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));

    drawTextureInternal(model, textureIdx, tint, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.y});
}


void FQuadRender::drawText(const std::string& text, const glm::vec3& position, const glm::vec4& color, Font* font)
{
    float cursorX = position.x;
    float cursorY = position.y;

    // Debug: draw baseline (optional, comment out later)
    // drawTexture(glm::vec3(position.x, position.y, 0.01f), glm::vec2(500, 2), nullptr, glm::vec4(0, 1, 0, 1));
    YA_CORE_ASSERT(font != nullptr, "TODO: font is null in Render2D::drawText, should make a default font");

    for (size_t i = 0; i < text.size(); ++i) {
        char             ch        = text[i];
        const Character& character = font->getCharacter(ch);

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

        ya::Ptr<Texture> texture = font->atlasTexture;
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