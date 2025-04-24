#pragma once

#include "Render/Texture.h"
#include "SDL3/SDL_gpu.h"

namespace SDL
{

struct SDLDevice;

class SDLTexture : public Texture
{
  private:
    SDLDevice &device;

    SDL_GPUTexture *textureHandle = nullptr;
    uint32_t        width         = 0;
    uint32_t        height        = 0;
    ETextureFormat  format        = ETextureFormat::R8G8B8A8_UNORM;
    ETextureType    type          = ETextureType::Texture2D;
    std::string     name;

  public:

    SDLTexture(SDLDevice &device);
    ~SDLTexture() override;


    // Factory methods for SDL implementation

    // Implementation of base class methods
    uint32_t           GetWidth() const override { return width; }
    uint32_t           GetHeight() const override { return height; }
    ETextureFormat     GetFormat() const override { return format; }
    ETextureType       GetType() const override { return type; }
    const std::string &GetName() const override { return name; }
    void              *GetNativeHandle() const override { return textureHandle; }

    bool Resize(uint32_t width, uint32_t height, std::shared_ptr<CommandBuffer> commandBuffer) override;
    bool UpdateData(const void *data, uint32_t width, uint32_t height, std::shared_ptr<CommandBuffer> commandBuffer) override;

    // SDL specific methods
    SDL_GPUTexture *GetSDLTexture() const { return textureHandle; }

    // Helper functions
    static SDL_GPUTextureFormat ConvertToSDLFormat(ETextureFormat format);
    static ETextureFormat       ConvertFromSDLFormat(SDL_GPUTextureFormat format);
    static SDL_GPUTextureType   ConvertToSDLType(ETextureType type);
    static ETextureType         ConvertFromSDLType(SDL_GPUTextureType type);

    bool createFromFile(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer);

    bool createFromBuffer(const void *data, uint32_t width, uint32_t height,
                          ETextureFormat format, const std::string &name,
                          std::shared_ptr<CommandBuffer> commandBuffer);

    bool createEmpty(uint32_t width, uint32_t height,
                     ETextureFormat format, ETextureUsage usage,
                     std::shared_ptr<CommandBuffer> commandBuffer);
};

} // namespace SDL