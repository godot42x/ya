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
#include "Render/TextureLibrary.h"
#include "imgui.h"

#include "Core/AssetManager.h"

#include "utility.cc/ranges.h"

#define DYN_CULL 1

namespace ya
{

static FRender2dData   render2dData;
static ICommandBuffer *curCmdBuf = nullptr;
static FQuadData      *quadData  = nullptr;


void Render2D::init(IRender *render, IRenderPass *renderpass)
{
    auto *swapchain = render->getSwapchain();
    auto  extent    = swapchain->getExtent();

    render2dData.windowWidth  = extent.width;
    render2dData.windowHeight = extent.height;

    MessageBus::get()->subscribe<WindowResizeEvent>([render](const WindowResizeEvent &ev) {
        auto *swapchain = render->getSwapchain();
        auto  extent    = swapchain->getExtent();
        YA_CORE_INFO("Window resized, swapchain extend: {}x{}, event: {}x{}", extent.width, extent.height, ev.GetWidth(), ev.GetHeight());
        render2dData.windowWidth  = extent.width;
        render2dData.windowHeight = extent.height;
        return false;
    });

    quadData = new FQuadData();
    quadData->init(render, renderpass);
}

void Render2D::destroy()
{
    quadData->destroy();
    delete quadData;
}

void Render2D::onUpdate()
{
}

void Render2D::begin(ICommandBuffer *cmdBuf)
{
    curCmdBuf = cmdBuf;
    quadData->begin();
}

void Render2D::onImGui()
{
#if DYN_CULL
    int cull = (int)(render2dData.cullMode);
    if (ImGui::Combo("Cull Mode", &cull, "None\0Front\0Back\0FrontAndBack\0")) {
        switch (cull) {
        case 0:
            render2dData.cullMode = ECullMode::None;
            break;
        case 1:
            render2dData.cullMode = ECullMode::Front;
            break;
        case 2:
            render2dData.cullMode = ECullMode::Back;
            break;
        case 3:
            render2dData.cullMode = ECullMode::FrontAndBack;
            break;
        default:
            render2dData.cullMode = ECullMode::Back;
            break;
        }
    }
#endif
    quadData->onImGui();
}

void Render2D::end()
{
    quadData->end();
    curCmdBuf = nullptr;
}

void Render2D::makeSprite(const glm::vec3         &position,
                          const glm::vec2         &size,
                          std::shared_ptr<Texture> texture,
                          const glm::vec4         &tint,
                          const glm::vec2         &uvScale)
{
    if (quadData->shouldFlush()) {
        quadData->flush(curCmdBuf);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));


    uint32_t textureIdx = 0; // white texture
    if (texture) {
        auto it = quadData->_textureLabel2Idx.find(texture->getLabel());
        if (it != quadData->_textureLabel2Idx.end())
        {
            textureIdx = it->second;
        }
        else {
            // TODO: use map to cache same texture view
            quadData->_textureViews.push_back(TextureView{
                .texture = texture,
                .sampler = TextureLibrary::getDefaultSampler(),
            });
            auto idx                                         = static_cast<uint32_t>(quadData->_textureViews.size() - 1);
            quadData->_textureLabel2Idx[texture->getLabel()] = idx;
            textureIdx                                       = idx;
        }
    }

    for (int i = 0; i < 4; i++) {
        // rotation -> scale -> offset
        *quadData->vertexPtr = FQuadData::Vertex{
            .pos        = model * FQuadData::vertices[i],
            .color      = tint,
            .texCoord   = FQuadData::defaultTexcoord[i] * uvScale,
            .textureIdx = textureIdx,
        };
        ++quadData->vertexPtr;
    }

    quadData->vertexCount += 4;
    quadData->indexCount += 6;
}

// MARK: quad init

void FQuadData::init(IRender *render, IRenderPass *renderPass)
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
            .usage         = EBufferUsage::UniformBuffer,
            .size          = sizeof(FrameUBO),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
            .label         = "Sprite2D_FrameUBO",
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
                    .pitch = sizeof(FQuadData::Vertex),
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
                .width    = static_cast<float>(render2dData.windowWidth),
                .height   = static_cast<float>(render2dData.windowHeight),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            }},
            .scissors  = {Scissor{
                 .offsetX = 0,
                 .offsetY = 0,
                 .width   = render2dData.windowWidth,
                 .height  = render2dData.windowHeight,
            }},
        },
    });

    _vertexBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .usage         = EBufferUsage::VertexBuffer | EBufferUsage::TransferDst,
            .size          = sizeof(FQuadData::Vertex) * MaxVertexCount,
            .memProperties = EMemoryProperty::HostVisible,
            .label         = "Sprite2D_VertexBuffer",
        });

    vertexPtr     = _vertexBuffer->map<FQuadData::Vertex>();
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
            .usage         = EBufferUsage::IndexBuffer | EBufferUsage::TransferDst,
            .data          = indices.data(),
            .size          = sizeof(uint32_t) * MaxIndexCount,
            .memProperties = EMemoryProperty::DeviceLocal,
            .label         = "Sprite2D_IndexBuffer",
        });



    // Note: White texture and default sampler are now managed by TextureLibrary
}


void FQuadData::destroy()
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

void FQuadData::begin()
{
    _textureViews.clear();
    _textureLabel2Idx.clear();
    _textureViews.push_back(
        TextureView{
            .texture = TextureLibrary::getWhiteTexture(),
            .sampler = TextureLibrary::getDefaultSampler(),
        });


    float w      = (float)render2dData.windowWidth;
    float h      = (float)render2dData.windowHeight;
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
    // flip y axis
    // proj[1][1] *= -1;

    updateFrameUBO(glm::mat4(1.0) * proj);
}


void FQuadData::end()
{
    quadData->flush(curCmdBuf);
}

// MARK: quad flush
void FQuadData::flush(ICommandBuffer *cmdBuf)
{
    if (vertexCount <= 0) {
        return;
    }
    updateResources();

    _vertexBuffer->flush();

    // Pipeline bind using abstract command buffer interface
    _pipeline->bind(cmdBuf->getHandle());

    // Set viewport, scissor and cull mode using abstract interface
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(render2dData.windowWidth), static_cast<float>(render2dData.windowHeight), 0.0f, 1.0f);
    cmdBuf->setScissor(0, 0, render2dData.windowWidth, render2dData.windowHeight);
#if DYN_CULL
    cmdBuf->setCullMode(render2dData.cullMode);
#endif

    // Bind descriptor sets using abstract interface
    std::vector<DescriptorSetHandle> descriptorSets = {_frameUboDS, _resourceDS};
    cmdBuf->bindDescriptorSets(_pipelineLayout->getHandle(), 0, descriptorSets);

    // Bind vertex and index buffers using abstract interface
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false);

    // Draw indexed
    cmdBuf->drawIndexed(static_cast<uint32_t>(indexCount), 1, 0, 0, 0);

    vertexPtr   = vertexPtrHead;
    vertexCount = 0;
    indexCount  = 0;
}

void FQuadData::updateFrameUBO(glm::mat4 viewProj)
{
    FrameUBO ubo{
        .matViewProj = viewProj,
    };
    _frameUBOBuffer->writeData(&ubo, sizeof(ubo), 0);

    DescriptorBufferInfo bufferInfo(BufferHandle(_frameUBOBuffer->getHandle()), 0, static_cast<uint64_t>(sizeof(FrameUBO)));

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

void FQuadData::updateResources()
{
    std::vector<DescriptorImageInfo> imageInfos;
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
                TextureLibrary::getDefaultSampler()->getHandle(),
                TextureLibrary::getWhiteTexture()->getImageViewHandle(),
                EImageLayout::ShaderReadOnlyOptimal);
        }
    }

    auto *descriptorHelper = _render->getDescriptorHelper();
    descriptorHelper->updateDescriptorSets(
        {
            IDescriptorSetHelper::genImageWrite(
                _resourceDS,
                0,
                0,
                EPipelineDescriptorType::CombinedImageSampler,
                imageInfos.data(),
                static_cast<uint32_t>(imageInfos.size())),
        },
        {});
}


} // namespace ya