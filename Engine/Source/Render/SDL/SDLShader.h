#pragma once

#include "Render/Render.h"
#include "Render/Shader.h"



struct SDLShader
{
    SDL_GPUShader                                                         *vertexShader   = nullptr;
    SDL_GPUShader                                                         *fragmentShader = nullptr;
    std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> shaderResources;
    SDL_GPUShaderCreateInfo                                                vertexCreateInfo;
    SDL_GPUShaderCreateInfo                                                fragmentCreateInfo;

    void preCreate(const ShaderCreateInfo &shaderCI)
    {

        ShaderScriptProcessorFactory factory;
        factory.withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
            .withShaderStoragePath("Engine/Shader/GLSL")
            .withCachedStoragePath("Engine/Intermediate/Shader/GLSL");

        std::shared_ptr<ShaderScriptProcessor> processor = factory.FactoryNew();

        auto ret = processor->process(shaderCI.shaderName);
        if (!ret) {
            NE_CORE_ERROR("Failed to process shader: {}", processor->tempProcessingPath);
            NE_CORE_ASSERT(false, "Failed to process shader: {}", processor->tempProcessingPath);
        }
        ShaderScriptProcessor::stage2spirv_t &codes = ret.value();

        // Process each shader stage and store both SPIRV-Cross resources and our custom reflection data
        for (const auto &[stage, code] : codes) {
            // Get shader resources from SPIR-V reflection using our new custom reflection
            ShaderReflection::ShaderResources reflectedResources = processor->reflect(stage, code);

            // Also store the original SPIRV-Cross resources for backward compatibility
            shaderResources[stage] = reflectedResources;
        }

        // Create shader create info for vertex shader
        vertexCreateInfo = {
            .code_size            = codes[EShaderStage::Vertex].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)codes[EShaderStage::Vertex].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = (Uint32)shaderResources[EShaderStage::Vertex].sampledImages.size(),
            .num_storage_textures = 0, // We're not using storage images currently
            .num_storage_buffers  = 0, // We're not using storage buffers currently
            .num_uniform_buffers  = (Uint32)shaderResources[EShaderStage::Vertex].uniformBuffers.size(),
        };

        // Create shader create info for fragment shader
        fragmentCreateInfo = {
            .code_size            = codes[EShaderStage::Fragment].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)codes[EShaderStage::Fragment].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = (Uint32)shaderResources[EShaderStage::Fragment].sampledImages.size(),
            .num_storage_textures = 0, // We're not using storage images currently
            .num_storage_buffers  = 0, // We're not using storage buffers currently
            .num_uniform_buffers  = [&]() -> Uint32 {
                const auto vertexUniformCount   = shaderResources[EShaderStage::Vertex].uniformBuffers.size();
                const auto fragmentUniformCount = shaderResources[EShaderStage::Fragment].uniformBuffers.size();
                if (vertexUniformCount + fragmentUniformCount > 99999) {
                    NE_CORE_ERROR("Combined uniform buffer count exceeds the maximum allowed: Vertex={}, Fragment={}", vertexUniformCount, fragmentUniformCount);
                    NE_CORE_ASSERT(false, "Uniform buffer count mismatch");
                }
                return static_cast<Uint32>(vertexUniformCount + fragmentUniformCount);
            }(),
        };
    }


    void create(SDL_GPUDevice *device)
    {
        bool ok      = true;
        vertexShader = SDL_CreateGPUShader(device, &vertexCreateInfo);
        if (!vertexShader) {
            NE_CORE_ERROR("Failed to create vertex shader");
            ok = false;
        }
        fragmentShader = SDL_CreateGPUShader(device, &fragmentCreateInfo);
        if (!fragmentShader) {
            NE_CORE_ERROR("Failed to create fragment shader");
            ok = false;
        }

        if (!ok) {
            clean(device);
        }
    }

    void clean(SDL_GPUDevice *device)
    {
        if (vertexShader) {
            SDL_ReleaseGPUShader(device, vertexShader);
        }
        if (fragmentShader) {
            SDL_ReleaseGPUShader(device, fragmentShader);
        }
    }
};