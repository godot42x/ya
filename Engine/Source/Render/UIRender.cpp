#include "UIRender.h"

#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"


static struct QuadData
{
    uint32_t m_maxVertices;
    uint32_t m_maxIndices;
    uint32_t m_maxTextureSlots{32};

    std::unordered_map<FName, std::shared_ptr<Texture2D>> m_textureCache;
    std::shared_ptr<Texture2D>                            m_whiteTexture{0}; // 用于纯色渲染

    std::array<glm::vec2, 4> quadVertices; // 四个顶点

    // 统计信息
    struct RenderStats
    {
        uint32_t drawCalls{0};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        uint32_t elementCount{0};
    } m_stats;
} quadData;

struct PushConstant
{
    glm::mat4 viewProjectionMatrix;
    // ...
};

struct VertexInput
{
    glm::mat4 modelMatrix; // 变换矩阵
    glm::vec2 texCoord;
    float     textureIndex;
    // ...
};

bool F2DRender::initialize(uint32_t maxVertices,
                           uint32_t maxIndices)
{
    auto  app      = Neon::App::get();
    auto  render   = app->getRender();
    auto *vkRender = static_cast<VulkanRender *>(render);

    // VulkanUtils::copyBuffer(
    //     vkRender->getCommandBuffer(),
    //     quadData.quadVertices.data(),
    //     maxVertices * sizeof(glm::vec2),
    //     vkRender->getVertexBuffer());
    return false;
}
