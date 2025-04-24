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
    std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> cachedShaderResources;



    struct CameraData
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
    };

    CameraData cameraData;


    bool init(SDL_GPUDevice *device, SDL_Window *window, const GraphicsPipelineCreateInfo &pipelineCI)
    {
        this->device = device;
        this->window = window;


        bool ok = _pipeline.create(device, window, pipelineCI);
        NE_CORE_ASSERT(ok, "Failed to create graphics pipeline: {}", SDL_GetError());

        return _pipeline.pipeline;
    }

    void clean();

    void beginFrame(SDL_GPURenderPass *renderpass, Camera &camera)
    {
        SDL_BindGPUGraphicsPipeline(renderpass, _pipeline.pipeline);

        // Unifrom buffer should be update continuously, or we can use a ring buffer to store the data
        cameraData.view       = camera.getViewMatrix();
        cameraData.projection = camera.getProjectionMatrix();
    }


    struct Material
    {
    };

    struct Light
    {

    } light;

    // change the global unifrom (global light, camera, etc.)
    void prepareGlobal()
    {
        // SDL_BindGPUGraphicsPipeline(renderpass, _pipeline.pipeline);

        // // dose all unifroms need to be update each frame?
        // SDL_PushGPUVertexUniformData(commandBuffer, 0, &cameraData, sizeof(CameraData));
        // // SDL_PushGPUFragmentUniformData(commandBuffer, 0, &cameraData, sizeof(CameraData));
        // SDL_PushGPUFragmentUniformData(commandBuffer, 1, &light, sizeof(Light));
    }



    void draw(SDL_GPURenderPass *renderpass, SDL_GPUCommandBuffer *commandBuffer, const Camera &camera)
    {
        // // change uniforms by each elem's material for drawcall
        // SDL_PushGPUFragmentUniformData(commandBuffer, 0, material.get(), sizeof(Material));


        // SDL_GPUBufferBinding vertexBufferBinding = {
        //     .buffer = vertexBuffer,
        //     .offset = 0,
        // };
        // SDL_BindGPUVertexBuffers(renderpass, 0, &vertexBufferBinding, 1);

        // // TODO: use uint16  to optimize index buffer
        // SDL_GPUBufferBinding indexBufferBinding = {
        //     .buffer = indexBuffer,
        //     .offset = 0,
        // };
        // SDL_BindGPUIndexBuffer(renderpass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }
};

} // namespace SDL
