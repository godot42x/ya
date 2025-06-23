#pragma once

#include "Render/Render.h"
#include "Render/Shader.h"

namespace SDL
{

// Helper function to convert SPIRV types to SDL_GPU vertex element formats
inline SDL_GPUVertexElementFormat spirvType2SDLFormat(const spirv_cross::SPIRType &type)
{
    // Check for alignment - we need to handle different data types
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::Float:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        break;

    case spirv_cross::SPIRType::Int:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_INT4;
        break;

    case spirv_cross::SPIRType::UInt:
        if (type.vecsize == 1 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
        else if (type.vecsize == 2 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT2;
        else if (type.vecsize == 3 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT3;
        else if (type.vecsize == 4 && type.columns == 1)
            return SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
        break;

    default:
        break;
    }

    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

struct SDLShaderProcessor
{

    SDL_GPUDevice                                                         &device;
    SDL_GPUShader                                                         *vertexShader   = nullptr;
    SDL_GPUShader                                                         *fragmentShader = nullptr;
    std::unordered_map<EShaderStage::T, ShaderReflection::ShaderResources> shaderResources;
    SDL_GPUShaderCreateInfo                                                vertexCreateInfo;
    SDL_GPUShaderCreateInfo                                                fragmentCreateInfo;


    ShaderScriptProcessor::stage2spirv_t shaderCodes; // store the codes

    SDLShaderProcessor(SDL_GPUDevice &device) : device(device) {}

    SDLShaderProcessor &preprocess(const ShaderCreateInfo &shaderCI)
    {

        std::shared_ptr<ShaderScriptProcessor> processor =
            ShaderScriptProcessorFactory()
                .withProcessorType(ShaderScriptProcessorFactory::EProcessorType::GLSL)
                .withShaderStoragePath("Engine/Shader/GLSL")
                .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                .FactoryNew<GLSLScriptProcessor>();


        auto ret = processor->process(shaderCI.shaderName);
        if (!ret) {
            NE_CORE_ERROR("Failed to process shader: {}", processor->tempProcessingPath);
            NE_CORE_ASSERT(false, "Failed to process shader: {}", processor->tempProcessingPath);
        }
        // store the temp codes
        shaderCodes = std::move(ret.value());

        // Process each shader stage and store both SPIRV-Cross resources and our custom reflection data
        for (const auto &[stage, code] : shaderCodes) {
            // Get shader resources from SPIR-V reflection using our new custom reflection
            ShaderReflection::ShaderResources reflectedResources = processor->reflect(stage, code);

            // Also store the original SPIRV-Cross resources for backward compatibility
            shaderResources[stage] = reflectedResources;
        }

        // Create shader create info for vertex shader
        vertexCreateInfo = {
            .code_size            = shaderCodes[EShaderStage::Vertex].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)shaderCodes[EShaderStage::Vertex].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers         = (Uint32)shaderResources[EShaderStage::Vertex].sampledImages.size(),
            .num_storage_textures = 0, // We're not using storage images currently
            .num_storage_buffers  = 0, // We're not using storage buffers currently
            .num_uniform_buffers  = (Uint32)shaderResources[EShaderStage::Vertex].uniformBuffers.size(),
            .props                = 0,

        };

        // Create shader create info for fragment shader
        fragmentCreateInfo = {
            .code_size            = shaderCodes[EShaderStage::Fragment].size() * sizeof(uint32_t) / sizeof(uint8_t),
            .code                 = (uint8_t *)shaderCodes[EShaderStage::Fragment].data(),
            .entrypoint           = "main",
            .format               = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage                = SDL_GPU_SHADERSTAGE_FRAGMENT,
            .num_samplers         = (Uint32)shaderResources[EShaderStage::Fragment].sampledImages.size(),
            .num_storage_textures = 0, // We're not using storage images currently
            .num_storage_buffers  = 0, // We're not using storage buffers currently
            .num_uniform_buffers  = [&]() -> Uint32 {
                const auto fragmentUniformCount = shaderResources[EShaderStage::Fragment].uniformBuffers.size();
                const auto samplerCount         = shaderResources[EShaderStage::Fragment].sampledImages.size();
                if (samplerCount + fragmentUniformCount > 99999) {
                    NE_CORE_ERROR("Combined uniform buffer count exceeds the maximum allowed: Vertex={}, Fragment={}", samplerCount, fragmentUniformCount);
                    NE_CORE_ASSERT(false, "Uniform buffer count mismatch");
                }

                // The sampler count is added to the fragment uniform count because both contribute to the total uniform buffer usage.
                NE_CORE_DEBUG("Fragment shader uniform count: {}, sampler count: {}", fragmentUniformCount, samplerCount);
                return static_cast<Uint32>(samplerCount + fragmentUniformCount);
            }(),
        };

        return *this;
    }


    SDLShaderProcessor &create()
    {
        bool ok      = true;
        vertexShader = SDL_CreateGPUShader(&device, &vertexCreateInfo);
        if (!vertexShader) {
            NE_CORE_ERROR("Failed to create vertex shader");
            ok = false;
        }
        fragmentShader = SDL_CreateGPUShader(&device, &fragmentCreateInfo);
        if (!fragmentShader) {
            NE_CORE_ERROR("Failed to create fragment shader");
            ok = false;
        }

        if (!ok) {
            clean();
        }
        return *this;
    }

    void clean()
    {
        if (vertexShader) {
            SDL_ReleaseGPUShader(&device, vertexShader);
        }
        if (fragmentShader) {
            SDL_ReleaseGPUShader(&device, fragmentShader);
        }
    }
};

} // namespace SDL