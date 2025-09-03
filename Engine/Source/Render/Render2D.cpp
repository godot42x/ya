#include "Render2D.h"

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Render/Render.h"


namespace ya
{



static void                 *curCmdBuf = nullptr;
static VulkanPipelineLayout *layout    = nullptr;

struct FQuadData
{

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
        glm::vec2 texCoord;
        float     rotation;
    };

    struct InstanceData
    {
        glm::mat4 modelMatrix;
    };

    static constexpr const std::array<glm::vec4, 4> vertices = {
        {
            {-0.5f, -0.5f, 0.0f, 1.f}, // LT
            {0.5f, -0.5f, 0.0f, 1.f},  // RT
            {0.5f, 0.5f, 0.0f, 1.f},   // RB
            {-0.5f, 0.5f, 0.0f, 1.f},  // LB
        }};

    static constexpr size_t MaxVertexCount = 10000;
    static constexpr size_t MaxIndexCount  = MaxVertexCount * 6 / 4; // 6 indices per quad, 4 vertices per quad

    VkDevice        logicalDevice = nullptr;
    VulkanPipeline *pipeline      = nullptr;

    std::shared_ptr<VulkanBuffer> vertexBuffer;
    std::shared_ptr<VulkanBuffer> indexBuffer;

    std::shared_ptr<VulkanBuffer> vertexStagingBuffer;

    FQuadData::Vertex *vertexPtr     = nullptr;
    FQuadData::Vertex *vertexPtrHead = nullptr;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;

    void init(VulkanRender *render, VulkanRenderPass *renderPass, VulkanPipelineLayout *layout)
    {
        logicalDevice = render->getLogicalDevice();
        pipeline      = new VulkanPipeline(render, renderPass, layout);
        pipeline->recreate(GraphicsPipelineCreateInfo{
            .subPassRef       = 0,
            .shaderCreateInfo = ShaderCreateInfo{
                .shaderName        = "Sprite2D.glsl",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot  = 0,
                        .pitch = static_cast<uint32_t>(T2Size(EVertexAttributeFormat::Float3)),
                    },
                },
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
                    VertexAttribute{
                        .bufferSlot = 0,
                        .location   = 2,
                        .format     = EVertexAttributeFormat::Float2,
                        .offset     = offsetof(Vertex, texCoord),
                    },
                    VertexAttribute{
                        .bufferSlot = 0,
                        .location   = 3,
                        .format     = EVertexAttributeFormat::Float,
                        .offset     = offsetof(Vertex, rotation),
                    },
                },
            },
            // define what state need to dynamically modified in render pass execution
            .dynamicFeatures    = {},
            .primitiveType      = EPrimitiveType::TriangleList,
            .rasterizationState = RasterizationState{
                .polygonMode = EPolygonMode::Fill,
                .frontFace   = EFrontFaceType::CounterClockWise, // GL
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
                           .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                    },

                },
            },
            .viewportState = ViewportState{

                .viewports = {
                    {
                        .x        = 0,
                        .y        = 0,
                        .width    = static_cast<float>(render->getSwapChain()->getWidth()),
                        .height   = static_cast<float>(render->getSwapChain()->getHeight()),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f,
                    },
                },
                .scissors = {Scissor{
                    .offsetX = 0,
                    .offsetY = 0,
                    .width   = static_cast<uint32_t>(render->getSwapChain()->getWidth()),
                    .height  = static_cast<uint32_t>(render->getSwapChain()->getHeight()),
                }},
            },
        });

        vertexBuffer = VulkanBuffer::create(
            render,
            BufferCreateInfo{
                .usage         = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .size          = sizeof(FQuadData::Vertex) * MaxVertexCount,
                .memProperties = 0,
                .debugName     = "Sprite2D_VertexBuffer",
            });
        vertexStagingBuffer = VulkanBuffer::create(
            render,
            BufferCreateInfo{
                .usage         = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                .size          = sizeof(FQuadData::Vertex) * MaxVertexCount,
                .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                .debugName     = "Sprite2D_VertexStagingBuffer",
            });

        vertexPtr     = vertexStagingBuffer->map<FQuadData::Vertex>();
        vertexPtrHead = vertexPtr;


        std::vector<uint32_t> indices;
        indices.reserve(MaxIndexCount);
        for (uint32_t i = 0; i < MaxVertexCount / 4; i++) {
            // 0,4,2,0,2,1
            indices.push_back(i * 4 + 0);
            indices.push_back(i * 4 + 4);
            indices.push_back(i * 4 + 2);

            indices.push_back(i * 4 + 0);
            indices.push_back(i * 4 + 2);
            indices.push_back(i * 4 + 1);
        }

        // index: 0,2,
        indexBuffer = VulkanBuffer::create(
            render,
            BufferCreateInfo{
                .usage         = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .data          = indices.data(),
                .size          = sizeof(uint32_t) * MaxIndexCount,
                .memProperties = 0,
                .debugName     = "Sprite2D_IndexBuffer",
            });
    }

    void destroy()
    {

        vertexBuffer.reset();
        indexBuffer.reset();
        vertexStagingBuffer.reset();

        delete pipeline;
    }

    void bind(void *cmdBuf)
    {
    }

    bool shouldFlush()
    {
        return vertexCount >= MaxVertexCount - 4;
    }

    void copyPass(void *cmdBuf)
    {
        std::size_t vertexCount = vertexPtr - vertexPtrHead;
        if (vertexCount <= 0) {
            return;
        }
        vertexPtr = vertexPtrHead;

        // TODO: unmap?
        VulkanBuffer::transfer((VkCommandBuffer)cmdBuf,
                               vertexStagingBuffer->getHandle(),
                               vertexBuffer->getHandle(),
                               sizeof(Vertex) * vertexCount);
    }

    void flush(void *cmdBuf)
    {

        VkBuffer     vertexBuffers[] = {vertexBuffer->getHandle()};
        VkDeviceSize offsets[]       = {0};
        pipeline->bind((VkCommandBuffer)cmdBuf);
        vkCmdBindVertexBuffers((VkCommandBuffer)cmdBuf,
                               0, // first binding
                               1, // binding count
                               vertexBuffers,
                               offsets);
        vkCmdBindIndexBuffer((VkCommandBuffer)cmdBuf,
                             indexBuffer->getHandle(),
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
        vertexCount = 0;
        indexCount  = 0;
    }



} quadData;



void Render2D::init(IRender *render, VulkanRenderPass *renderpass)
{
    auto *vkRender = dynamic_cast<VulkanRender *>(render);

    PipelineLayout pipelineLayout{
        .pushConstants = {
            PushConstantRange{
                .offset     = 0,
                .size       = sizeof(float),
                .stageFlags = EShaderStage::Vertex,
            },
        },
        .descriptorSetLayouts = {
            // DescriptorSetLayout{
            //     .set      = 0,
            //     .bindings = {
            //         // uGBuffer
            //         DescriptorSetLayoutBinding{
            //             .binding         = 0,
            //             .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Vertex,
            //         },
            //         // uInstanceBuffer
            //         DescriptorSetLayoutBinding{
            //             .binding         = 1,
            //             .descriptorType  = EPipelineDescriptorType::UniformBuffer,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Vertex,
            //         },
            //         // uTexture0
            //         DescriptorSetLayoutBinding{
            //             .binding         = 2,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            //         // uTexture1
            //         DescriptorSetLayoutBinding{
            //             .binding         = 3,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 1,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            //         // uTextures
            //         DescriptorSetLayoutBinding{
            //             .binding         = 4,
            //             .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            //             .descriptorCount = 16,
            //             .stageFlags      = EShaderStage::Fragment,
            //         },
            // },
            // },
        },
    };

    layout = new VulkanPipelineLayout(vkRender);
    // layout->create(pipelineLayout.pushConstants, { pipelineLayout.descriptorSetLayouts[0], });
    layout->create(pipelineLayout.pushConstants, {});

    quadData.init(vkRender, renderpass, layout);
}

void Render2D::destroy()
{
    quadData.destroy();
    delete layout;
}

void Render2D::onUpdate()
{
    quadData.copyPass(curCmdBuf);
}

void Render2D::begin(void *cmdBuf)
{
    curCmdBuf = cmdBuf;
}

void Render2D::end()
{
    quadData.flush(curCmdBuf);
    curCmdBuf = nullptr;
}

void Render2D::makeSprite(const glm::vec3 &position, const glm::vec2 &size, const glm::vec4 &color)
{
    if (quadData.shouldFlush()) {
        quadData.flush(curCmdBuf);
    }

    for (int i = 0; i < 4; i++) {
        quadData.vertexPtr->pos      = glm::vec3(FQuadData::vertices[i] *
                                            glm::translate(glm::mat4(1.f), glm::vec3(position)) *
                                            glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f)));
        quadData.vertexPtr->texCoord = glm::vec2(i % 2, i / 2);
        quadData.vertexPtr->color    = color;
        quadData.vertexPtr->rotation = 0.0f;

        ++quadData.vertexPtr;
        quadData.vertexCount += 4;
        quadData.indexCount += 6;
    }
}


} // namespace ya