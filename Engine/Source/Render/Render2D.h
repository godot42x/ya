
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

struct Command
{
    enum ECmd
    {
        DrawQuad,
        DrawText,
        ENUM_MAX
    } cmd;

    union
    {
        struct
        {
            glm::vec3 position;
            glm::vec2 size;
            glm::vec4 color;
        } drawQuad;

        struct
        {
            std::string text;
            glm::vec2   position;
            glm::vec4   color;
        } drawText;
    };
};

struct CmdList
{
    std::vector<Command> commands;
};

struct Render2D
{
    static CmdList cmdList;

    Render2D()          = default;
    virtual ~Render2D() = default;


    static void init(IRender *render, VulkanRenderPass *renderpass);
    static void destroy();
    struct InstanceData
    {
        glm::mat4 modelMatrix;
    };


    static void onUpdate();
    static void begin(void *cmdBuf);
    static void onImGui();
    static void end();

    // 绘制矩形
    static void makeSprite(const glm::vec3 &position, const glm::vec2 &size, const glm::vec4 &color);

    static CmdList& getCmdList() { return Render2D::cmdList; }

    // void makeRotatedSprite(const glm::vec2 &position, const glm::vec2 &size, const glm::vec4 &color, float rotation) {}
    // void drawText(const std::string &text, const glm::vec2 &position, const glm::vec4 &color) {}
};
}; // namespace ya