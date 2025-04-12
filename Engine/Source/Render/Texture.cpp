#include "Texture.h"

#include "Render/SDL/SDLTexture.h"



// Implementation of Texture's static factory methods

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