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

} // namespace SDL
