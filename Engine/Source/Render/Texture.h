#pragma once

#include "Device.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

// Forward declarations
struct SDL_GPUTexture;
class CommandBuffer;

enum class ETextureFormat
{
    R8G8B8A8_UNORM,
    R8G8B8_UNORM,
    RGBA32_FLOAT,
    // Add more formats as needed
};

enum class ETextureType
{
    Texture2D,
    CubeMap,
    // Add more types as needed
};

enum class ETextureUsage
{
    Sampler,
    RenderTarget,
    DepthStencil,
    // Add more usages as needed
};

class Texture
{

  public:
    virtual ~Texture() = default;

    // Create texture methods
    static std::shared_ptr<Texture> CreateFromBuffer(const void *data,
                                                     uint32_t width, uint32_t height,
                                                     ETextureFormat format, const std::string &name,
                                                     std::shared_ptr<CommandBuffer> commandBuffer);
    static std::shared_ptr<Texture> CreateEmpty(uint32_t width, uint32_t height,
                                                ETextureFormat format, ETextureUsage usage,
                                                std::shared_ptr<CommandBuffer> commandBuffer);

    // Getters
    virtual uint32_t           GetWidth() const  = 0;
    virtual uint32_t           GetHeight() const = 0;
    virtual ETextureFormat     GetFormat() const = 0;
    virtual ETextureType       GetType() const   = 0;
    virtual const std::string &GetName() const   = 0;

    // Utility methods
    virtual bool  Resize(uint32_t width, uint32_t height, std::shared_ptr<CommandBuffer> commandBuffer)                       = 0;
    virtual bool  UpdateData(const void *data, uint32_t width, uint32_t height, std::shared_ptr<CommandBuffer> commandBuffer) = 0;
    virtual void *GetNativeHandle() const                                                                                     = 0; // Get the native handle of the texture (e.g., SDL_GPUTexture* for SDL)

    std::shared_ptr<Texture> CreateFromFile(LogicalDevice &device, const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer);


  protected:
    Texture() = default;
};