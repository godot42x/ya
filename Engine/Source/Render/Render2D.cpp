#include "Render2D.h"

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Render/Render.h"


namespace ya
{


VkBuffer          vertexBuffer              = VK_NULL_HANDLE;
VkDeviceMemory    vertexBufferMemory        = VK_NULL_HANDLE;
VkBuffer          vertexStagingBuffer       = VK_NULL_HANDLE;
VkDeviceMemory    vertexStagingBufferMemory = VK_NULL_HANDLE;
Render2D::Vertex *vertexPtr                 = nullptr;
Render2D::Vertex *vertexPtrHead             = nullptr;

VkBuffer       indexBuffer              = VK_NULL_HANDLE;
VkDeviceMemory indexBufferMemory        = VK_NULL_HANDLE;
VkBuffer       indexStagingBuffer       = VK_NULL_HANDLE;
VkDeviceMemory indexStagingBufferMemory = VK_NULL_HANDLE;

void Render2D::init(IRender *render, VulkanPipelineLayout *layout, VulkanRenderPass *renderpass)
{
    auto *vkRender = dynamic_cast<VulkanRender *>(render);

    pipeline = new VulkanPipeline(vkRender, renderpass, layout);
    pipeline->recreate(GraphicsPipelineCreateInfo{
        // .pipelineLayout   = pipelineLayout,
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
                    .offset     = 0,
                },
                // aColor
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float4,
                    // .offset     = offsetof(VertexInput, texCoord),
                },
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float3,
                    // .offset     = offsetof(VertexInput, normal),
                },

            },
        },
        .subPassRef = 0,
        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures    = {},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
        },
        .multisampleState  = MultisampleState{},
        .depthStencilState = DepthStencilState{},
        .colorBlendState   = ColorBlendState{
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
        .viewportState = ViewportState{},
    });
}

void Render2D::begin()
{
}

void Render2D::end(void *cmdBuf)
{
    auto        vkRender = static_cast<VulkanRender *>(render);
    std::size_t count    = vertexPtr - vertexPtrHead;
    vertexPtr            = vertexPtrHead;

    VulkanBuffer::transfer(vkRender,
                           vertexStagingBuffer,
                           vertexBuffer,
                           sizeof(Vertex) * count);

    // TODO: turn vertex data into instance data instead?
    // vertexBuffer contains the constant data: rawPos, texCoord
    // do the model matrix  multiply the pos in GPU or CPU?
    vkCmdDrawIndexed((VkCommandBuffer)cmdBuf,
                     static_cast<uint32_t>(count),
                     1,  // instance count
                     0,  // first index
                     0,  // vertex offset
                     0); // first instance
}

void Render2D::makeSprite(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color)
{
    for (int i = 0; i < 4; i++) {
        vertexPtr->aPosition  = {position, 0.0f};
        vertexPtr->aTexCoord  = glm::vec2(i % 2, i / 2);
        vertexPtr->aColor     = color;
        vertexPtr->aTextureId = 0; // TODO: assign actual texture ID
        vertexPtr->aRotation  = 0.0f;

        ++vertexPtr;
    }
}

void Render2D::initBuffers()
{

    VulkanRender *vkRender = static_cast<VulkanRender *>(render);

    constexpr uint32_t maxVertices = 10000; // Adjust as needed
    constexpr uint32_t maxIndices  = maxVertices * 1.5;

    VulkanBuffer::allocate(vkRender,
                           sizeof(Vertex) * maxVertices,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           vertexBuffer,
                           vertexBufferMemory);
    VulkanBuffer::allocate(vkRender,
                           sizeof(Vertex) * maxVertices,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           vertexStagingBuffer,
                           vertexStagingBufferMemory);

    vkMapMemory(vkRender->getLogicalDevice(),
                vertexStagingBufferMemory,
                0,
                sizeof(Vertex) * maxVertices,
                0,
                (void **)&vertexPtr);


    VulkanBuffer::allocate(vkRender,
                           sizeof(uint32_t) * maxIndices,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           indexBuffer,
                           indexBufferMemory);
    VulkanBuffer::allocate(vkRender,
                           sizeof(uint32_t) * maxIndices,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           indexStagingBuffer,
                           indexStagingBufferMemory);
}


} // namespace ya