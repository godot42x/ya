#include "SDLTexture.h"


#include <SDL3/SDL_gpu.h>
#include <SDL3_image/SDL_image.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Log.h"
#include "Render/CommandBuffer.h"
#include "SDLGPUCommandBuffer.h"
#include "SDLGPURender3D.h"
#include "SDLHelper.h"



namespace SDL
{


SDLTexture::SDLTexture(SDLDevice &device)
    : device(device)
{
}

SDLTexture::~SDLTexture()
{
    if (textureHandle) {
        SDL_ReleaseGPUTexture(device.getNativeDevicePtr<SDL_GPUDevice>(), textureHandle);
        textureHandle = nullptr;
    }
}

// SDLTexture implementation

bool SDLTexture::createFromFile(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlCommandBuffer = commandBuffer->getNativeCommandBufferPtr<SDL_GPUCommandBuffer>();
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return false;
    }

    auto         path    = FileSystem::get()->getProjectRoot() / filepath;
    SDL_Surface *surface = IMG_Load(path.string().c_str());
    if (!surface) {
        NE_CORE_ERROR("Failed to load image: {}", SDL_GetError());
        return false;
    }

    SDL_GPUTexture          *texture = nullptr;
    SDL_GPUTextureCreateInfo info{
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = static_cast<Uint32>(surface->w),
        .height               = static_cast<Uint32>(surface->h),
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };

    texture = SDL_CreateGPUTexture(device.getNativeDevicePtr<SDL_GPUDevice>(), &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        SDL_DestroySurface(surface);
        return false;
    }

    auto filename = std::format("{}", path.stem().string());
    SDL_SetGPUTextureName(device.getNativeDevicePtr<SDL_GPUDevice>(), texture, filename.c_str());


    SDLHelper::uploadTexture(device.getNativeDevicePtr<SDL_GPUDevice>(),
                             sdlCommandBuffer,
                             texture,
                             surface->pixels,
                             surface->w,
                             surface->h);

    SDL_DestroySurface(surface);
    return true;
}

bool SDLTexture::createFromBuffer(const void *data, uint32_t width, uint32_t height,
                                  ETextureFormat format, const std::string &name,
                                  std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlDevice        = device.getNativeDevicePtr<SDL_GPUDevice>();
    auto *sdlCommandBuffer = commandBuffer->getNativeCommandBufferPtr<SDL_GPUCommandBuffer>();
    if (!sdlCommandBuffer)
    {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return false;
    }

    SDL_GPUTexture          *texture = nullptr;
    SDL_GPUTextureCreateInfo info{
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = ConvertToSDLFormat(format),
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = width,
        .height               = height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };

    texture = SDL_CreateGPUTexture(sdlDevice, &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        return false;
    }

    SDL_SetGPUTextureName(sdlDevice, texture, name.c_str());

    SDLHelper::uploadTexture(sdlDevice,
                             sdlCommandBuffer,
                             texture,
                             (void *)data,
                             width,
                             height);

    return true;
}

bool SDLTexture::createEmpty(uint32_t width, uint32_t height,
                             ETextureFormat format, ETextureUsage usage,
                             std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlDevice        = device.getNativeDevicePtr<SDL_GPUDevice>();
    auto *sdlCommandBuffer = dynamic_cast<SDLGPUCommandBuffer *>(commandBuffer.get());
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return false;
    }

    SDL_GPUTexture          *texture = nullptr;
    SDL_GPUTextureCreateInfo info{
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = ConvertToSDLFormat(format),
        .usage                = (usage == ETextureUsage::RenderTarget)
                                    ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                : (usage == ETextureUsage::DepthStencil)
                                    ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                    : SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = width,
        .height               = height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };

    texture = SDL_CreateGPUTexture(sdlDevice, &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create empty texture: {}", SDL_GetError());
        return false;
    }

    std::string name = "EmptyTexture";
    SDL_SetGPUTextureName(sdlDevice, texture, name.c_str());


    return true;
}

bool SDLTexture::Resize(uint32_t newWidth, uint32_t newHeight, std::shared_ptr<CommandBuffer> commandBuffer)
{
    throw std::runtime_error("Resize not implemented for SDLTexture");
}

bool SDLTexture::UpdateData(const void *data, uint32_t newWidth, uint32_t newHeight, std::shared_ptr<CommandBuffer> commandBuffer)
{
    throw std::runtime_error("UpdateData not implemented for SDLTexture");
}

SDL_GPUTextureFormat SDLTexture::ConvertToSDLFormat(ETextureFormat format)
{
    switch (format) {
    case ETextureFormat::R8G8B8A8_UNORM:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case ETextureFormat::R8G8B8_UNORM:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // Note: SDL might not have direct R8G8B8 format
    case ETextureFormat::RGBA32_FLOAT:
        return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    default:
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
}

ETextureFormat SDLTexture::ConvertFromSDLFormat(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return ETextureFormat::R8G8B8A8_UNORM;
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
        return ETextureFormat::RGBA32_FLOAT;
    default:
        return ETextureFormat::R8G8B8A8_UNORM;
    }
}

SDL_GPUTextureType SDLTexture::ConvertToSDLType(ETextureType type)
{
    switch (type) {
    case ETextureType::Texture2D:
        return SDL_GPU_TEXTURETYPE_2D;
    case ETextureType::CubeMap:
        return SDL_GPU_TEXTURETYPE_CUBE;
    default:
        return SDL_GPU_TEXTURETYPE_2D;
    }
}

ETextureType SDLTexture::ConvertFromSDLType(SDL_GPUTextureType type)
{
    switch (type) {
    case SDL_GPU_TEXTURETYPE_2D:
        return ETextureType::Texture2D;
    case SDL_GPU_TEXTURETYPE_CUBE:
        return ETextureType::CubeMap;
    default:
        return ETextureType::Texture2D; // Default to Texture2D for unknown types
    }
}

} // namespace SDL
