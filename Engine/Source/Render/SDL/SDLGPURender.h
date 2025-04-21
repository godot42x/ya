#pragma once
#include "Core/Camera.h"
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

#include "SDLGraphicsPipeline.h"

namespace SDL
{

struct SDLRender3D
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
    SDLGraphicsPipeLine                                                    _pipeline;
    SDL_GPUBuffer                                                         *vertexBuffer;
    SDL_GPUBuffer                                                         *indexBuffer;
    std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> cachedShaderResources;

    uint32_t maxVertexBufferElemSize = 1000 * 1024 * 1024; // 1MB
    uint32_t maxIndexBufferElemSize  = 1000 * 1024 * 1024; // 1MB
    uint32_t vertexInputSize         = 0;

    uint32_t getVertexBufferSize() const { return maxVertexBufferElemSize * vertexInputSize; }
    uint32_t getIndexBufferSize() const { return maxIndexBufferElemSize * sizeof(uint32_t); }



    bool init(bool bVsync);
    void clean();
    bool createGraphicsPipeline(const GraphicsPipelineCreateInfo &info);

    void beginFrame(SDL_GPURenderPass *renderpass, Camera &camera)
    {
        SDL_BindGPUGraphicsPipeline(renderpass, _pipeline.pipeline);

        // Unifrom buffer should be update continuously, or we can use a ring buffer to store the data
        // cameraData.view       = camera.getViewMatrix();
        // cameraData.projection = camera.getProjectionMatrix();
    }

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




    std::shared_ptr<CommandBuffer> acquireCommandBuffer(std::source_location location = std::source_location::current());


};

} // namespace SDL
