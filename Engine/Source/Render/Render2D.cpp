#include "Render2D.h"

#include "Core/MessageBus.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Render/Render.h"
#include "imgui.h"



namespace ya
{



static void                 *curCmdBuf = nullptr;
static VulkanPipelineLayout *layout    = nullptr;

struct FRender2dData
{
    uint32_t windowWidth  = 800;
    uint32_t windowHeight = 600;
} render2dData;

struct FQuadData
{

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
        glm::vec2 texCoord;
    };

    struct InstanceData
    {
        glm::mat4 modelMatrix;
    };

    static constexpr const std::array<glm::vec4, 4> vertices = {
        {
            // {-1.0f, -1.0f, 0.0f, 1.f}, // LT
            // {1.0f, -1.0f, 0.0f, 1.f},  // RT
            // {-1.0f, 1.0f, 0.0f, 1.f},  // LB
            // {1.0f, 1.0f, 0.0f, 1.f},   // RB

            {0.0f, 0.0f, 0.0f, 1.f}, // LT
            {1.0f, 0.0f, 0.0f, 1.f}, // RT
            {0.0f, 1.0f, 0.0f, 1.f}, // LB
            {1.0f, 1.0f, 0.0f, 1.f}, // RB
        }};
    static constexpr const std::array<glm::vec2, 4> defaultTexcoord = {
        {
            {0, 0}, // LT
            {1, 0}, // RT
            {0, 1}, // LB
            {1, 1}, // RB
        }};

    static constexpr size_t MaxVertexCount = 10000;
    static constexpr size_t MaxIndexCount  = MaxVertexCount * 6 / 4; // 6 indices per quad, 4 vertices per quad

    VkDevice        logicalDevice = nullptr;
    VulkanPipeline *pipeline      = nullptr;

    std::shared_ptr<VulkanBuffer> vertexBuffer;
    std::shared_ptr<VulkanBuffer> indexBuffer;

    FQuadData::Vertex *vertexPtr     = nullptr;
    FQuadData::Vertex *vertexPtrHead = nullptr;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;


    Viewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = 800,
        .height   = 600,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    Scissor scissor{
        .offsetX = 0,
        .offsetY = 0,
        .width   = 800,
        .height  = 600,
    };
    Rect2D cameraRect{
        .pos    = {0, 0},
        .extent = {800, 600},
    };



    void init(VulkanRender *render, VulkanRenderPass *renderPass, VulkanPipelineLayout *layout)
    {

        auto winW                 = render->getSwapChain()->getExtent().width;
        auto winH                 = render->getSwapChain()->getExtent().height;
        render2dData.windowWidth  = winW;
        render2dData.windowHeight = winH;

        viewport = Viewport{
            .x        = 0,
            .y        = 0,
            .width    = static_cast<float>(winW),
            .height   = static_cast<float>(winH),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        scissor = Scissor{
            .offsetX = 0,
            .offsetY = 0,
            .width   = winW,
            .height  = winH,
        };
        cameraRect = Rect2D{
            .pos    = {0, 0},
            .extent = {
                static_cast<float>(winW),
                static_cast<float>(winH),
            },
        };


        logicalDevice = render->getLogicalDevice();
        pipeline      = new VulkanPipeline(render, renderPass, layout);
        pipeline->recreate(GraphicsPipelineCreateInfo{
            .subPassRef       = 0,
            .shaderCreateInfo = ShaderCreateInfo{
                .shaderName        = "Sprite2D.glsl",
                .bDeriveFromShader = false,
                .vertexBufferDescs = {
                    VertexBufferDescription{
                        .slot = 0,
                        // ??? no copy pasted!!! errror
                        .pitch = sizeof(FQuadData::Vertex),
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
                },
            },
            // define what state need to dynamically modified in render pass execution
            .dynamicFeatures    = EPipelineDynamicFeature::Viewport | EPipelineDynamicFeature::Scissor,
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
                .viewports = {viewport},
                .scissors  = {scissor},
            },
        });

        vertexBuffer = VulkanBuffer::create(
            render,
            BufferCreateInfo{
                .usage         = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .size          = sizeof(FQuadData::Vertex) * MaxVertexCount,
                .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                .debugName     = "Sprite2D_VertexBuffer",
            });

        vertexPtr     = vertexBuffer->map<FQuadData::Vertex>();
        vertexPtrHead = vertexPtr;


        std::vector<uint32_t> indices;
        indices.resize(MaxIndexCount);
        // constant indices?
        for (uint32_t i = 0; i < MaxIndexCount; i += 6) {

            uint32_t vertexIndex = (i / 6) * 4;

            // quad-> 2 triangle-> counter-clockwise  0,3,1,0,3,2
            indices[i + 0] = vertexIndex + 0;
            indices[i + 1] = vertexIndex + 1; // 3;
            indices[i + 2] = vertexIndex + 2; // 1;

            indices[i + 3] = vertexIndex + 1; // 0;
            indices[i + 4] = vertexIndex + 3; // 2;
            indices[i + 5] = vertexIndex + 2; // 3;
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


        MessageBus::get().subscribe<WindowResizeEvent>([render](const WindowResizeEvent &) {
            auto winW                 = render->getSwapChain()->getExtent().width;
            auto winH                 = render->getSwapChain()->getExtent().height;
            render2dData.windowWidth  = winW;
            render2dData.windowHeight = winH;
            return false;
        });
    }

    void destroy()
    {
        // MessageBus::get().d
        vertexBuffer.reset();
        indexBuffer.reset();

        delete pipeline;
    }


    void onImGui()
    {
        if (ImGui::CollapsingHeader("Render2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            // control viewports &scissors
            ImGui::DragFloat2("Viewport Pos", &viewport.x);
            ImGui::DragFloat2("Viewport Size", &viewport.width);

            ImGui::DragInt2("Scissor Offset", (int *)&scissor.offsetX);
            ImGui::DragInt2("Scissor Extent", (int *)&scissor.width);

            ImGui::DragFloat4("Camera Rect", &cameraRect.pos.x, 1.0f);
        }
    }

    bool shouldFlush()
    {
        return vertexCount >= MaxVertexCount - 4;
    }

    void flush(void *cmdBuf)
    {
        auto curCmdBuf = (VkCommandBuffer)cmdBuf;
        if (vertexCount <= 0) {
            return;
        }

        vertexBuffer->flush();

        VkBuffer     vertexBuffers[] = {vertexBuffer->getHandle()};
        VkDeviceSize offsets[]       = {0};
        pipeline->bind((VkCommandBuffer)cmdBuf);

        VkViewport vp{
            .x        = viewport.x,
            .y        = viewport.y,
            .width    = viewport.width,
            .height   = viewport.height,
            .minDepth = viewport.minDepth,
            .maxDepth = viewport.maxDepth,
        };
        vkCmdSetViewport(curCmdBuf, 0, 1, &vp);
        VkRect2D vkScissor{
            .offset = {
                scissor.offsetX,
                scissor.offsetY,
            },
            .extent = {
                static_cast<uint32_t>(scissor.width),
                static_cast<uint32_t>(scissor.height),
            },
        };
        vkCmdSetScissor(curCmdBuf, 0, 1, &vkScissor);

        vkCmdBindVertexBuffers(curCmdBuf,
                               0, // first binding
                               1, // binding count
                               vertexBuffers,
                               offsets);
        vkCmdBindIndexBuffer(curCmdBuf,
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

        vertexPtr   = vertexPtrHead;
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
        .descriptorSetLayouts = {},
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
}

void Render2D::begin(void *cmdBuf)
{
    curCmdBuf = cmdBuf;
}

void Render2D::onImGui()
{
    quadData.onImGui();
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
     */

    // from LT (0,0) to RB (w,h)
    glm::mat4 proj = glm::orthoLH_NO(0.0f,
                                     w,

                                     // because glm use bottom-left as origin, we need to flip the y axis
                                     h,
                                     0.0f,

                                     -1.0f,
                                     1.0f);
    // glm::lookAt()

    glm::mat4 mvp = glm::inverse(proj) *
                    // glm::inverse(view) *
                    // model
                    glm::translate(glm::mat4(1.f), position) *
                    glm::scale(glm::mat4(1.f), glm::vec3(size, 1.0f));
    // glm::rotate(glm::mat4(1.f), glm::radians(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));


    for (int i = 0; i < 4; i++) {
        quadData.vertexPtr->pos      = mvp * FQuadData::vertices[i];
        quadData.vertexPtr->texCoord = FQuadData::defaultTexcoord[i];
        quadData.vertexPtr->color    = color;
        ++quadData.vertexPtr;
    }

    quadData.vertexCount += 4;
    quadData.indexCount += 6;
}


} // namespace ya