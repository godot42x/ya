#pragma once
#include "Render/Render.h"

#include <memory>
#include <optional>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>


#include "Render/CommandBuffer.h"
#include "Render/Shader.h"

namespace SDL
{

struct GPURender_SDL : public Render
{
    SDL_GPUDevice *device;
    SDL_Window    *window;

    // Pipeline management
    enum class PipelineType
    {
        Model3D = 0,
        Sprite2D,
        Count
    };

    // Legacy support - points to the current active pipeline
    SDL_GPUGraphicsPipeline                                               *pipeline;
    SDL_GPUBuffer                                                         *vertexBuffer;
    SDL_GPUBuffer                                                         *indexBuffer;
    std::unordered_map<ESamplerType, SDL_GPUSampler *>                     samplers;
    std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> cachedShaderResources;

    uint32_t maxVertexBufferElemSize = 1000 * 1024 * 1024; // 1MB
    uint32_t maxIndexBufferElemSize  = 1000 * 1024 * 1024; // 1MB
    uint32_t vertexInputSize         = 0;

    uint32_t getVertexBufferSize() const { return maxVertexBufferElemSize * vertexInputSize; }
    uint32_t getIndexBufferSize() const { return maxIndexBufferElemSize * sizeof(uint32_t); }



    bool init(const InitParams &params) override;
    void clean() override;
    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info) override;

    void *getNativeDevice()
    {
        return device;
    }
    void *getNativeWindow()
    {
        return window;
    }

    struct ShaderCreateResult
    {
        std::optional<SDL_GPUShader *>                                         vertexShader;
        std::optional<SDL_GPUShader *>                                         fragmentShader;
        std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> shaderResources;
    };



    ShaderCreateResult createShaders(const ShaderCreateInfo &shaderCI);

    std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current()) override;


  private:
    void createSamplers();
};

class RenderPassManager
{
  public:
    enum class RenderStage
    {
        Setup,       // 初始化渲染通道、清除颜色和深度
        Background,  // 背景渲染
        World3D,     // 3D世界对象渲染
        Transparent, // 半透明物体渲染
        UI2D,        // 2D UI渲染
        Debug,       // 调试元素渲染
        Count
    };

    struct RenderCommand
    {
        RenderStage                              stage;
        std::function<void(SDL_GPURenderPass *)> renderFunc;
        int                                      priority = 0;
    };

    void init(SDL_GPUDevice *device);
    void cleanup();

    // 添加渲染命令到特定阶段
    void addRenderCommand(RenderStage                              stage,
                          std::function<void(SDL_GPURenderPass *)> renderFunc,
                          int                                      priority = 0);

    // 执行所有阶段的渲染
    void executeRenderPass(SDL_GPUCommandBuffer *cmdBuffer,
                           SDL_GPUTexture       *colorTarget,
                           SDL_GPUTexture       *depthTarget,
                           const SDL_Color      &clearColor);

  private:
    SDL_GPUDevice                          *device = nullptr;
    std::vector<std::vector<RenderCommand>> stageCommands;
};


} // namespace SDL
