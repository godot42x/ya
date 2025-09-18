#include "Render2D.h"

#include "Core/MessageBus.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Render/Render.h"
#include "imgui.h"

#include "Core/AssetManager.h"

#include "utility.cc/ranges.h"

#define DYN_CULL 1

namespace ya
{

static FRender2dData render2dData;
static void         *curCmdBuf = nullptr;
static FQuadData     quadData;


void Render2D::init(IRender *render, VulkanRenderPass *renderpass)
{
    auto *vkRender = dynamic_cast<VulkanRender *>(render);

    render2dData.windowWidth  = vkRender->getSwapChain()->getExtent().width;
    render2dData.windowHeight = vkRender->getSwapChain()->getExtent().height;

    MessageBus::get().subscribe<WindowResizeEvent>([vkRender](const WindowResizeEvent &ev) {
        auto winW = vkRender->getSwapChain()->getExtent().width;
        auto winH = vkRender->getSwapChain()->getExtent().height;
        YA_CORE_INFO("Window resized, swapchain extend: {}x{}, event: {}x{}", winW, winH, ev.GetWidth(), ev.GetHeight());
        render2dData.windowWidth  = winW;
        render2dData.windowHeight = winH;
        return false;
    });

    quadData.init(vkRender, renderpass);
}

void Render2D::destroy()
{
    quadData.destroy();
}

void Render2D::onUpdate()
{
}

void Render2D::begin(void *cmdBuf)
{
    curCmdBuf = cmdBuf;
    quadData.begin();
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
    quadData.onImGui();
}

void Render2D::end()
{
    quadData.end();
    curCmdBuf = nullptr;
}

void Render2D::makeSprite(const glm::vec3         &position,
                          const glm::vec2         &size,
                          const glm::vec4         &color,
                          const glm::vec2         &uvScale,
                          float                    uvRotation,
                          std::shared_ptr<Texture> texture)
{
    if (quadData.shouldFlush()) {
        quadData.flush(curCmdBuf);
    }
    auto app = App::get();

    glm::mat4 model = glm::translate(glm::mat4(1.f), {position.x, position.y, position.z}) *
                      glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));


    uint32_t textureIdx = 0; // white texture
    if (texture) {
        // TODO: use map to cache same texture view
        quadData._textureViews.push_back(TextureView{
            .texture = texture.get(),
            .sampler = app->_linearSampler.get(),
        });
        textureIdx = static_cast<uint32_t>(quadData._textureViews.size() - 1);
    }

    for (int i = 0; i < 4; i++) {
        *quadData.vertexPtr = FQuadData::Vertex{
            .pos   = model * FQuadData::vertices[i],
            .color = color,
            // TODO:
            //  1. pass the uv params to shader by vertex attributes or instance attributes or unifroms?
            //  2. push constants not good for large data batch draw call
            //  3. or compute them at CPU side?
            // glm::vec2 uv = FQuadData::defaultTexcoord[i] *
            //                glm::rotate(glm::mat4(1.0f), glm::radians(uvRotation), glm::vec3(0.0f, 0.0f, 1.0f)) *
            //              *glm::vec4(uvScale);
            .texCoord   = FQuadData::defaultTexcoord[i],
            .textureIdx = textureIdx,
        };
        ++quadData.vertexPtr;
    }

    quadData.vertexCount += 4;
    quadData.indexCount += 6;
}

// MARK: quad init

void FQuadData::init(VulkanRender *vkRender, VulkanRenderPass *renderPass)
{
    logicalDevice = vkRender->getDevice();

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
                .label    = "Texture_SAMPLER",
                .set      = 0,
                .bindings = {
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 32, // TODO: max 32 textures
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    _descriptorPool = makeShared<VulkanDescriptorPool>(
        vkRender,
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

    _frameUboDSL = makeShared<VulkanDescriptorSetLayout>(vkRender, _pipelineDesc.descriptorSetLayouts[0]);
    std::vector<VkDescriptorSet> descriptorSets;
    _descriptorPool->allocateDescriptorSetN(_frameUboDSL, 1, descriptorSets);
    _frameUboDS     = descriptorSets[0];
    _frameUBOBuffer = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .size          = sizeof(FrameUBO),
            .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .debugName     = "Sprite2D_FrameUBO",
        });

    _resourceDSL = makeShared<VulkanDescriptorSetLayout>(vkRender, _pipelineDesc.descriptorSetLayouts[1]);
    descriptorSets.clear();
    _descriptorPool->allocateDescriptorSetN(_resourceDSL, 1, descriptorSets);
    _resourceDS = descriptorSets[0];


    _pipelineLayout = makeShared<VulkanPipelineLayout>(vkRender);
    _pipelineLayout->create(
        _pipelineDesc.pushConstants,
        {
            _frameUboDSL->getHandle(),
            _resourceDSL->getHandle(),
        });

    {
        // update all texture descriptors to white texture
        auto app = App::get();
        for (uint32_t i = 0; i < TEXTURE_SET_SIZE; i++) {
            _textureViews.push_back(TextureView{
                .texture = app->_whiteTexture.get(),
                .sampler = app->_linearSampler.get(),
            });
        }
        updateResources(vkRender->getDevice());
    }

    _pipeline = makeShared<VulkanPipeline>(vkRender, renderPass, _pipelineLayout.get());
    _pipeline->recreate(GraphicsPipelineCreateInfo{
        .subPassRef       = 0,
        .shaderCreateInfo = ShaderCreateInfo{
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
        },
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = EPipelineDynamicFeature::Viewport | EPipelineDynamicFeature::Scissor
#if DYN_CULL
                         | EPipelineDynamicFeature::CullMode
#endif
        ,
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
                       .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
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

    _vertexBuffer = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .size          = sizeof(FQuadData::Vertex) * MaxVertexCount,
            .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .debugName     = "Sprite2D_VertexBuffer",
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

    _indexBuffer = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .data          = indices.data(),
            .size          = sizeof(uint32_t) * MaxIndexCount,
            .memProperties = 0,
            .debugName     = "Sprite2D_IndexBuffer",
        });
}


void FQuadData::destroy()
{
    _vertexBuffer.reset();
    _indexBuffer.reset();


    _pipeline.reset();
    _pipelineLayout.reset();
}

void FQuadData::begin()
{
    auto app = App::get();
    _textureViews.clear();
    _textureViews.push_back(
        TextureView{
            .texture = app->_whiteTexture.get(),
            .sampler = app->_linearSampler.get(),
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
    quadData.flush(curCmdBuf);
}

// MARK: quad flush
void FQuadData::flush(void *cmdBuf)
{
    auto curCmdBuf = (VkCommandBuffer)cmdBuf;
    if (vertexCount <= 0) {
        return;
    }
    updateResources(logicalDevice);
    _textureViews.resize(1); // remove other textures, only white texture

    _vertexBuffer->flush();

    VkBuffer     vertexBuffers[] = {_vertexBuffer->getHandle()};
    VkDeviceSize offsets[]       = {0};
    _pipeline->bind((VkCommandBuffer)cmdBuf);

    VkViewport vp{
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(render2dData.windowWidth),
        .height   = static_cast<float>(render2dData.windowHeight),
        .minDepth = 0,
        .maxDepth = 1,
    };
    vkCmdSetViewport(curCmdBuf, 0, 1, &vp);
    VkRect2D vkScissor{
        .offset = {
            0,
            0,
        },
        .extent = {
            render2dData.windowWidth,
            render2dData.windowHeight,
        },
    };
    vkCmdSetScissor(curCmdBuf, 0, 1, &vkScissor);
#if DYN_CULL
    vkCmdSetCullMode(curCmdBuf, render2dData.cullMode);
#endif


    std::vector<VkDescriptorSet> descriptorSets = {
        _frameUboDS,
        _resourceDS,
    };

    vkCmdBindDescriptorSets(curCmdBuf,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _pipelineLayout->getHandle(),
                            0, // first set
                            static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(),
                            0,
                            nullptr);
    vkCmdBindVertexBuffers(curCmdBuf,
                           0, // first binding
                           1, // binding count
                           vertexBuffers,
                           offsets);
    vkCmdBindIndexBuffer(curCmdBuf,
                         _indexBuffer->getHandle(),
                         0,
                         VK_INDEX_TYPE_UINT32);

    // TODO: turn vertex data into instance data instead?
    // vertexBuffer contains the constant data: rawPos, texCoord
    // do the model matrix  multiply the pos in GPU or CPU?
    vkCmdDrawIndexed((VkCommandBuffer)cmdBuf,
                     static_cast<uint32_t>(indexCount),
                     1,  // instance count
                     0,  // first index
                     0,  // vertex offset
                     0); // first instance

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

    VkDescriptorBufferInfo bufferInfo{
        .buffer = _frameUBOBuffer->getHandle(),
        .offset = 0,
        .range  = sizeof(FrameUBO),
    };

    VulkanDescriptor::updateSets(
        logicalDevice,
        {
            VulkanDescriptor::genBufferWrite(
                _frameUboDS,
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                &bufferInfo,
                1),
        },
        {});
}

void FQuadData::updateResources(VkDevice device)
{
    // TODO: 0 always white texture, don't need to update every frame
    std::vector<VkDescriptorImageInfo> imageInfos;
    for (TextureView &texView : _textureViews) {
        auto *tv = &texView;
        imageInfos.push_back(VkDescriptorImageInfo{
            .sampler     = tv->getSampler()->getHandle(),
            .imageView   = tv->getTexture()->getVkImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
    }

    VulkanDescriptor::updateSets(
        device,
        {
            VulkanDescriptor::genImageWrite(
                _resourceDS,
                0,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                imageInfos.data(),
                static_cast<uint32_t>(imageInfos.size())),
        },
        {});
}


} // namespace ya