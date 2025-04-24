#include "Texture.h"

#include "Platform/Render/SDL/SDLDevice.h"
#include "Platform/Render/SDL/SDLTexture.h"
#include "Render/Device.h"



// Implementation of Texture's static factory methods

std::shared_ptr<Texture> Texture::CreateFromFile(LogicalDevice &device, const std::string &filepath,
                                                 std::shared_ptr<CommandBuffer> commandBuffer)
{

    auto sdlTexture = std::make_shared<SDL::SDLTexture>(*static_cast<SDL::SDLDevice*>(&device));
    sdlTexture->createFromFile(filepath, commandBuffer);
    return sdlTexture;
}

std::shared_ptr<Texture> Texture::CreateFromBuffer(const void *data, uint32_t width, uint32_t height,
                                                   ETextureFormat format, const std::string &name,
                                                   std::shared_ptr<CommandBuffer> commandBuffer)
{
    return SDL::SDLTexture::CreateFromBuffer(data, width, height, format, name, commandBuffer);
}

std::shared_ptr<Texture> Texture::CreateEmpty(uint32_t width, uint32_t height,
                                              ETextureFormat format, ETextureUsage usage,
                                              std::shared_ptr<CommandBuffer> commandBuffer)
{
    return SDL::SDLTexture::CreateEmpty(width, height, format, usage, commandBuffer);
}