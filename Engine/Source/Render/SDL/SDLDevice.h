
#pragma once

#include <unordered_map>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "Render/Render.h"

namespace SDL
{


struct SDLDevice
{
    SDL_GPUDevice                                     *device = nullptr;
    SDL_Window                                        *window = nullptr;
    std::unordered_map<ESamplerType, SDL_GPUSampler *> samplers;


    struct InitParams
    {
        bool bVsync = true;
    };

    bool init(const InitParams &params)
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to initialize SDL: %s", SDL_GetError());
            return false;
        }

        int n = SDL_GetNumGPUDrivers();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%d Available drivers: ", n);
        for (int i = 0; i < n; ++i) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetGPUDriver(i));
        }

        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                         SDL_GPU_SHADERFORMAT_DXIL |
                                         SDL_GPU_SHADERFORMAT_MSL,
                                     true,
                                     nullptr);
        if (!device) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create GPU device: %s", SDL_GetError());
            return false;
        }

        const char *driver = SDL_GetGPUDeviceDriver(device);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Chosen GPU Driver: %s", driver);

        window = SDL_CreateWindow("Neon", 1024, 768, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create window: %s", SDL_GetError());
            return false;
        }

        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to claim window: %s", SDL_GetError());
            return false;
        }

        SDL_SetGPUSwapchainParameters(device,
                                      window,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      params.bVsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE);

        return true;
    }

    void createSamplers()
    {
        // DefaultLinear
        SDL_GPUSamplerCreateInfo defaultLinearInfo = {
            .min_filter        = SDL_GPU_FILTER_LINEAR, // 缩小采样
            .mag_filter        = SDL_GPU_FILTER_LINEAR, // 放大采样
            .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .mip_lod_bias      = 0.0,
            .compare_op        = SDL_GPU_COMPAREOP_ALWAYS,
            .min_lod           = 1.0,
            .max_lod           = 1.0,
            .enable_anisotropy = false,
            .enable_compare    = false,
        };
        samplers[ESamplerType::DefaultLinear] = SDL_CreateGPUSampler(device, &defaultLinearInfo);

        // DefaultNearest
        SDL_GPUSamplerCreateInfo defaultNearestInfo = defaultLinearInfo;
        defaultNearestInfo.min_filter               = SDL_GPU_FILTER_NEAREST;
        defaultNearestInfo.mag_filter               = SDL_GPU_FILTER_NEAREST;
        defaultNearestInfo.mipmap_mode              = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplers[ESamplerType::DefaultNearest]      = SDL_CreateGPUSampler(device, &defaultNearestInfo);

        // PointClamp
        SDL_GPUSamplerCreateInfo pointClampInfo = {
            .min_filter     = SDL_GPU_FILTER_NEAREST,
            .mag_filter     = SDL_GPU_FILTER_NEAREST,
            .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        samplers[ESamplerType::PointClamp] = SDL_CreateGPUSampler(device, &pointClampInfo);

        // PointWrap
        SDL_GPUSamplerCreateInfo pointWrapInfo = {
            .min_filter     = SDL_GPU_FILTER_NEAREST,
            .mag_filter     = SDL_GPU_FILTER_NEAREST,
            .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        };
        samplers[ESamplerType::PointWrap] = SDL_CreateGPUSampler(device, &pointWrapInfo);

        // LinearClamp
        SDL_GPUSamplerCreateInfo linearClampInfo = {
            .min_filter     = SDL_GPU_FILTER_LINEAR,
            .mag_filter     = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        };
        samplers[ESamplerType::LinearClamp] = SDL_CreateGPUSampler(device, &linearClampInfo);

        // LinearWrap
        SDL_GPUSamplerCreateInfo linearWrapInfo = {
            .min_filter     = SDL_GPU_FILTER_LINEAR,
            .mag_filter     = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        };
        samplers[ESamplerType::LinearWrap] = SDL_CreateGPUSampler(device, &linearWrapInfo);

        // AnisotropicClamp
        SDL_GPUSamplerCreateInfo anisotropicClampInfo = {
            .min_filter        = SDL_GPU_FILTER_LINEAR,
            .mag_filter        = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
            .max_anisotropy    = 4,
            .enable_anisotropy = true,
        };
        samplers[ESamplerType::AnisotropicClamp] = SDL_CreateGPUSampler(device, &anisotropicClampInfo);


        // AnisotropicWrap
        SDL_GPUSamplerCreateInfo anisotropicWrapInfo = {
            .min_filter        = SDL_GPU_FILTER_LINEAR,
            .mag_filter        = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .max_anisotropy    = 4,
            .enable_anisotropy = true,
        };
        samplers[ESamplerType::AnisotropicWrap] = SDL_CreateGPUSampler(device, &anisotropicWrapInfo);
        // validate samplers
        for (auto &[key, sampler] : samplers) {
            NE_CORE_ASSERT(sampler, "Failed to create sampler {} {}", (int)key, SDL_GetError());
        }
    }
};



}; // namespace SDL