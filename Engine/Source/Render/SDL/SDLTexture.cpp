#include "SDLTexture.h"


#include <SDL3/SDL_gpu.h>
#include <SDL3_image/SDL_image.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Log.h"
#include "Render/CommandBuffer.h"
#include "Render/SDL/SDLGPUCommandBuffer.h"
#include "Render/SDL/SDLGPURender.h"



namespace SDL
{


// SDLTexture implementation
SDLTexture::SDLTexture(SDL_GPUTexture *texture, uint32_t width, uint32_t height,
                       ETextureFormat format, ETextureType type, const std::string &name)
    : textureHandle(texture), width(width), height(height),
      format(format), type(type), name(name)
{
}

SDLTexture::~SDLTexture()
{
    if (textureHandle) {
        // Get SDL device from somewhere to release the texture
        // We could store a reference to the device or use a weak_ptr to the render
        // For now, we'll rely on SDLGPURender to handle cleanup of textures
        // SDL_ReleaseGPUTexture(device, textureHandle);
        // textureHandle = nullptr;
    }
}

std::shared_ptr<Texture> SDLTexture::Create(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlCommandBuffer = dynamic_cast<GPUCommandBuffer_SDL *>(commandBuffer.get());
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return nullptr;
    }

    auto         path    = FileSystem::get()->getProjectRoot() / filepath;
    SDL_Surface *surface = IMG_Load(path.string().c_str());
    if (!surface) {
        NE_CORE_ERROR("Failed to load image: {}", SDL_GetError());
        return nullptr;
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

    texture = SDL_CreateGPUTexture(sdlCommandBuffer->r->device, &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        SDL_DestroySurface(surface);
        return nullptr;
    }

    auto filename = std::format("{}", path.stem().string());
    SDL_SetGPUTextureName(sdlCommandBuffer->r->device, texture, filename.c_str());

    sdlCommandBuffer->uploadTexture(texture, surface->pixels, surface->w, surface->h);

    SDL_DestroySurface(surface);

    return std::make_shared<SDLTexture>(
        texture,
        info.width,
        info.height,
        ConvertFromSDLFormat(info.format),
        ConvertFromSDLType(info.type),
        filename);
}

std::shared_ptr<Texture> SDLTexture::CreateFromBuffer(const void *data, uint32_t width, uint32_t height,
                                                      ETextureFormat format, const std::string &name,
                                                      std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlCommandBuffer = dynamic_cast<GPUCommandBuffer_SDL *>(commandBuffer.get());
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return nullptr;
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

    texture = SDL_CreateGPUTexture(sdlCommandBuffer->r->device, &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create texture: {}", SDL_GetError());
        return nullptr;
    }

    SDL_SetGPUTextureName(sdlCommandBuffer->r->device, texture, name.c_str());

    sdlCommandBuffer->uploadTexture(texture, (void *)data, width, height);

    return std::make_shared<SDLTexture>(
        texture,
        width,
        height,
        format,
        ETextureType::Texture2D,
        name);
}

std::shared_ptr<Texture> SDLTexture::CreateEmpty(uint32_t width, uint32_t height,
                                                 ETextureFormat format, ETextureUsage usage,
                                                 std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto *sdlCommandBuffer = dynamic_cast<GPUCommandBuffer_SDL *>(commandBuffer.get());
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture creation");
        return nullptr;
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

    texture = SDL_CreateGPUTexture(sdlCommandBuffer->r->device, &info);
    if (!texture) {
        NE_CORE_ERROR("Failed to create empty texture: {}", SDL_GetError());
        return nullptr;
    }

    std::string name = "EmptyTexture";
    SDL_SetGPUTextureName(sdlCommandBuffer->r->device, texture, name.c_str());

    return std::make_shared<SDLTexture>(
        texture,
        width,
        height,
        format,
        ETextureType::Texture2D,
        name);
}

bool SDLTexture::Resize(uint32_t newWidth, uint32_t newHeight, std::shared_ptr<CommandBuffer> commandBuffer)
{
    // Recreate the texture with new dimensions, preserving contents if needed
    // For simplicity we just create a new texture
    auto *sdlCommandBuffer = dynamic_cast<GPUCommandBuffer_SDL *>(commandBuffer.get());
    if (!sdlCommandBuffer || !textureHandle) {
        return false;
    }

    SDL_GPUTextureCreateInfo info{
        .type                 = ConvertToSDLType(type),
        .format               = ConvertToSDLFormat(format),
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER, // We might need to pass the original usage
        .width                = newWidth,
        .height               = newHeight,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };

    SDL_GPUTexture *newTexture = SDL_CreateGPUTexture(sdlCommandBuffer->r->device, &info);
    if (!newTexture) {
        NE_CORE_ERROR("Failed to resize texture: {}", SDL_GetError());
        return false;
    }

    SDL_SetGPUTextureName(sdlCommandBuffer->r->device, newTexture, name.c_str());

    // Release old texture and update our handle
    SDL_ReleaseGPUTexture(sdlCommandBuffer->r->device, textureHandle);
    textureHandle = newTexture;
    width         = newWidth;
    height        = newHeight;

    return true;
}

bool SDLTexture::UpdateData(const void *data, uint32_t newWidth, uint32_t newHeight, std::shared_ptr<CommandBuffer> commandBuffer)
{
    if (!data || !textureHandle) {
        return false;
    }

    auto *sdlCommandBuffer = dynamic_cast<GPUCommandBuffer_SDL *>(commandBuffer.get());
    if (!sdlCommandBuffer) {
        NE_CORE_ERROR("Invalid command buffer for texture update");
        return false;
    }

    // Make sure dimensions match or resize
    if (newWidth != width || newHeight != height) {
        if (!Resize(newWidth, newHeight, commandBuffer)) {
            return false;
        }
    }

    // Upload the new data
    sdlCommandBuffer->uploadTexture(textureHandle, (void *)data, newWidth, newHeight);
    return true;
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
