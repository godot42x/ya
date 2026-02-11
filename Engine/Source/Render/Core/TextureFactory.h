#pragma once

#include "Core/Base.h"
#include "Image.h"
#include "Render/RenderDefines.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// Forward declarations
struct IImage;
struct IImageView;
struct IRender;
struct Texture;

// Forward declaration for ImageViewCreateInfo
struct ImageViewCreateInfo;

/**
 * @brief Texture creation info for fromData
 */
struct TextureDataCreateInfo
{
    std::string label;
    uint32_t    width    = 0;
    uint32_t    height   = 0;
    const void *data     = nullptr;
    size_t      dataSize = 0;
    EFormat::T  format   = EFormat::R8G8B8A8_UNORM;
    uint32_t    mipLevels = 1;
};

/**
 * @brief Texture creation info for render targets (simple texture-based RT)
 * Note: For full render target with multiple attachments, use RenderTargetCreateInfo in IRenderTarget.h
 */
struct RenderTextureCreateInfo
{
    std::string     label;
    uint32_t        width    = 0;
    uint32_t        height   = 0;
    EFormat::T      format   = EFormat::R8G8B8A8_UNORM;
    EImageUsage::T  usage    = EImageUsage::ColorAttachment;
    ESampleCount::T samples  = ESampleCount::Sample_1;
    bool            isDepth  = false;
};

/**
 * @brief Cube map face enum (for external use)
 */
enum ECubeFace
{
    CubeFace_PosX = 0,
    CubeFace_NegX,
    CubeFace_PosY,
    CubeFace_NegY,
    CubeFace_PosZ,
    CubeFace_NegZ,
    CubeFace_Count
};

/**
 * @brief CubeMap creation info
 */
struct CubeMapCreateInfo
{
    std::string                               label;
    std::array<std::string, CubeFace_Count>   files;
    bool                                      flipVertical = false;
};

/**
 * @brief ITextureFactory - RHI layer texture resource factory interface
 *
 * Responsibilities:
 * - Provide low-level IImage/IImageView creation API
 * - Isolate RHI layer from backend-specific implementation
 * - Support different rendering backends (Vulkan, OpenGL, etc.)
 *
 * Design Principles:
 * 1. Only provides low-level resource creation (IImage, IImageView)
 * 2. High-level Texture creation is handled by Texture's static methods
 * 3. Extensible: Support adding new rendering backends in the future
 *
 * API Design:
 * - createImage(): Create image resource
 * - createImageView(): Create image view resource
 * - createCubeMapImageView(): Create cubemap image view
 *
 * Note: Use Texture::fromFile(), Texture::createCubeMap() etc. for high-level API.
 */
struct ITextureFactory
{
    virtual ~ITextureFactory() = default;

    // ====== Low-level API (Used by Texture static methods) ======

    /**
     * @brief Create image resource
     * @param ci Image creation info
     * @return Shared pointer to IImage interface
     */
    virtual std::shared_ptr<IImage> createImage(const ImageCreateInfo &ci) = 0;

    /**
     * @brief Create image from existing platform-specific handle
     * @param platformImage Platform-specific image handle
     * @param format Image format
     * @param usage Image usage flags
     * @return Shared pointer to IImage interface
     */
    virtual std::shared_ptr<IImage> createImageFromHandle(void *platformImage, EFormat::T format, EImageUsage::T usage) = 0;

    /**
     * @brief Create image view
     * @param image Base image
     * @param aspectFlags Aspect flags for the view
     * @return Shared pointer to IImageView interface
     */
    virtual std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, uint32_t aspectFlags) = 0;

    /**
     * @brief Create image view with detailed config
     * @param image Base image
     * @param ci Image view creation info
     * @return Shared pointer to IImageView interface
     */
    virtual std::shared_ptr<IImageView> createImageView(std::shared_ptr<IImage> image, const ImageViewCreateInfo &ci) = 0;

    /**
     * @brief Create cubemap image view
     * @param image Base image
     * @param aspectFlags Aspect flags
     * @param baseMipLevel Base mip level
     * @param levelCount Mip level count
     * @param baseArrayLayer Base array layer
     * @param layerCount Layer count (6 for cubemap)
     * @return Shared pointer to IImageView interface
     */
    virtual std::shared_ptr<IImageView> createCubeMapImageView(
        std::shared_ptr<IImage> image,
        uint32_t                aspectFlags,
        uint32_t                baseMipLevel   = 0,
        uint32_t                levelCount     = 1,
        uint32_t                baseArrayLayer = 0,
        uint32_t                layerCount     = 6) = 0;

    /**
     * @brief Get associated renderer
     * @return IRender interface pointer
     */
    virtual IRender *getRender() const = 0;

    /**
     * @brief Check if factory is valid
     * @return Whether factory is initialized
     */
    virtual bool isValid() const = 0;
};

/**
 * @brief Image view creation info
 */
struct ImageViewCreateInfo
{
    std::string label;
    uint32_t    viewType       = 0;    // Platform-specific view type
    uint32_t    aspectFlags    = 0;    // Platform-specific aspect flags
    uint32_t    baseMipLevel   = 0;
    uint32_t    levelCount     = 1;
    uint32_t    baseArrayLayer = 0;
    uint32_t    layerCount     = 1;

    // Component mapping (for format conversion)
    struct ComponentMapping
    {
        uint32_t r = 0;
        uint32_t g = 0;
        uint32_t b = 0;
        uint32_t a = 0;
    } components;
};

/**
 * @brief TextureFactoryHelper - Global access to texture factory
 */
class TextureFactoryHelper
{
  public:
    /**
     * @brief Get current renderer's texture factory
     * @return ITextureFactory interface pointer, nullptr if not set
     */
    static ITextureFactory *get();

    /**
     * @brief Set current renderer's texture factory
     * @param factory Texture factory interface
     */
    static void set(ITextureFactory *factory);

    /**
     * @brief Check if texture factory is available
     * @return Whether factory is set and valid
     */
    static bool isAvailable();

  private:
    static ITextureFactory *s_currentFactory;
};

} // namespace ya