
#pragma once

#include "glm/glm.hpp"

#include "Core/App/App.h"
#include "Core/Base.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanRenderPass.h"


#include "Render/Render.h"

namespace ya
{

struct Render2D
{

    IRender          *render     = nullptr;
    VulkanRenderPass *renderpass = nullptr;
    VulkanPipeline   *pipeline   = nullptr;

    Render2D()          = default;
    virtual ~Render2D() = default;


    void init(IRender *render, VulkanPipelineLayout *layout, VulkanRenderPass *renderpass);


    void destroy()
    {
    }

    struct Vertex
    {
        glm::vec3 aPosition; // z is the layer id?
        glm::vec2 aTexCoord;
        glm::vec4 aColor;
        float     aTextureId;
        float     aRotation;
    };
    struct InstanceData
    {
        glm::mat4 modelMatrix;
    };


    virtual void begin();
    virtual void end(void *cmdBuf);

    // 绘制矩形
    void makeSprite(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color);
    void makeRotatedSprite(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color, float rotation);

    // 绘制文本
    virtual void drawText(const std::string &text, const glm::vec2 &position, const glm::vec4 &color) = 0;

    void initBuffers();
};
}; // namespace ya