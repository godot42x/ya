#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>
#include <string>

namespace ya
{

struct Texture;

struct RenderOverlaySprite2D
{
    glm::vec2 viewportPos = glm::vec2(0.0f);
    glm::vec2 size        = glm::vec2(50.0f);
    Texture*  texture     = nullptr;
    glm::vec4 tint        = glm::vec4(1.0f);
};

struct RenderOverlaySprite3D
{
    glm::mat4 worldTransform = glm::mat4(1.0f);
    Texture*  texture        = nullptr;
    glm::vec4 tint           = glm::vec4(1.0f);
};

struct RenderOverlayText2D
{
    std::string text{};
    glm::vec2   viewportPos = glm::vec2(0.0f);
    glm::vec4   color       = glm::vec4(1.0f);
    uint32_t    fontSize    = 16;
    float       depth       = 0.0f;
};

} // namespace ya
