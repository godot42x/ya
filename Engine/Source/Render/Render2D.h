#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <stdexcept>
#include <string>


#include "Render/Render.h" // 引入 RenderAPI 枚举
#include "Render/Texture.h"



template <typename BackendT>
concept TRenderTrait = requires(BackendT backend, glm::vec2 pos, float rotation, std::shared_ptr<Texture> texture, float tillingFactor) {
    {
        backend.drawQuads(pos, rotation, texture, tillingFactor)
    };
};

struct RenderBackend
{
};


// 用户友好的渲染API，隐藏模板参数和后端实例
class Render2D
{
  private:
    // 各种后端实例

    static inline RenderAPI::T s_currentBackend = RenderAPI::None;
    static inline void        *currentBackend   = nullptr; // Vulkan 后端实例

  public:
    // 设置要使用的后端
    static void setBackend(RenderAPI::T api)
    {
        if (api <= RenderAPI::None || api >= RenderAPI::ENUM_MAX)
        {
            throw std::runtime_error("Invalid render backend API");
        }
        s_currentBackend = api;
    }

    // 获取当前使用的后端类型
    static RenderAPI::T getCurrentBackend()
    {
        return s_currentBackend;
    }

    // 包装的渲染方法，根据当前设置的后端自动选择实现
    static void drawQuads(glm::vec2 pos, float rotation, std::shared_ptr<Texture> texture, float tillingFactor)
    {
        currentBackend::drawQuads(pos, rotation, texture, tillingFactor);
    }
};

// 使用示例
void a()
{
    // 1. 设置要使用的后端（只需在启动时设置一次）
    Render2D::setBackend(RenderAPI::Vulkan);

    // 2. 直接调用方法，不需要指定后端或模板参数
    glm::vec2                pos           = {0.0f, 0.0f};
    float                    rotation      = 0.0f;
    std::shared_ptr<Texture> texture       = nullptr;
    float                    tillingFactor = 1.0f;


    Render2D::drawQuads(pos, rotation, texture, tillingFactor);
}