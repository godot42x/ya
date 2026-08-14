#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Image.h"
#include "RHI/RenderDefines.h"
#include "Sampler.h"
#include "TextureCreateInfo.h"

#include <array>
#include <vector>

namespace ya
{

struct IRender;
struct IRenderResourceFactory;

// Forward declaration
template <typename ComponentType>
struct ColorRGBA
{
    using ty = ComponentType;
    ty r;
    ty g;
    ty b;
    ty a;
};

struct ITexture
{
};

using ColorU8_t = ColorRGBA<uint8_t>;

struct TextureMemoryView
{
    uint32_t    width     = 0;
    uint32_t    height    = 0;
    uint32_t    channels  = 4;
    uint32_t    mipLevels = 1;
    bool        generateMipmaps = false;
    EFormat::T  format    = EFormat::R8G8B8A8_UNORM;
    const void* data      = nullptr;
    size_t      dataSize  = 0;

    [[nodiscard]] size_t baseLevelDataSize() const
    {
        const auto blockExtent = EFormat::getCompressedBlockExtent(format);
        const bool bCompressed = EFormat::isBlockCompressed(format);
        const size_t blocksX = bCompressed
                                   ? (static_cast<size_t>(width) + blockExtent.width - 1) / blockExtent.width
                                   : width;
        const size_t blocksY = bCompressed
                                   ? (static_cast<size_t>(height) + blockExtent.height - 1) / blockExtent.height
                                   : height;
        return blocksX * blocksY * EFormat::getPixelSize(format);
    }

    [[nodiscard]] bool isValid() const
    {
        return data != nullptr && width > 0 && height > 0 &&
               dataSize >= baseLevelDataSize();
    }
};

struct TextureMemoryCreateInfo
{
    std::string       filepath;
    std::string       label;
    TextureMemoryView memory;
};

struct CubeMapMemoryCreateInfo
{
    std::string                                   label;
    std::array<TextureMemoryView, CubeFace_Count> faces{};
    bool                                          flipVertical = false;

    [[nodiscard]] bool isValid() const
    {
        if (!faces[0].isValid()) {
            return false;
        }

        const auto width    = faces[0].width;
        const auto height   = faces[0].height;
        const auto channels = faces[0].channels;
        const auto format   = faces[0].format;

        for (const auto& face : faces) {
            if (!face.isValid()) {
                return false;
            }

            if (face.width != width || face.height != height ||
                face.channels != channels || face.format != format) {
                return false;
            }
        }

        return true;
    }
};

struct YA_RHI_API Texture
{
    EFormat::T _format    = EFormat::R8G8B8A8_UNORM;
    uint32_t   _width     = 0;
    uint32_t   _height    = 0;
    uint32_t   _channels  = 4; // RGBA by default
    uint32_t   _mipLevels = 1; // Number of mip levels

    std::string _label;
    std::string _filepath;

    std::shared_ptr<IImage>     image;
    std::shared_ptr<IImageView> imageView;
    std::vector<std::shared_ptr<void>> retainedResources;

  public:

    static std::shared_ptr<Texture> fromMemory(IRender& render, const TextureMemoryCreateInfo& ci);

    static std::shared_ptr<Texture> fromData(IRender&                           render,
                                             uint32_t                           width,
                                             uint32_t                               height,
                                             const std::vector<ColorRGBA<uint8_t>>& data,
                                             const std::string&                     label = "");

    static std::shared_ptr<Texture> fromData(IRender&           render,
                                             uint32_t           width,
                                             uint32_t           height,
                                             const void*        data,
                                             size_t             dataSize,
                                             EFormat::T         format,
                                             const std::string& label = "");

    static std::shared_ptr<Texture> createCubeMap(IRender& render, const CubeMapCreateInfo& ci);
    static std::shared_ptr<Texture> createCubeMapFromMemory(IRender& render, const CubeMapMemoryCreateInfo& ci);
    static std::shared_ptr<Texture> createSolidCubeMap(IRender& render, const ColorU8_t&   color,
                                                       const std::string& label = "");

    /**
     * @brief Wrap existing IImage/IImageView into a Texture
     * @param img Existing image
     * @param view Existing image view
     * @param label Optional label for debugging
     * @return Shared pointer to Texture
     *
     * Usage (internal): auto tex = Texture::wrap(vkImage, vkImageView, "label");
     */
    static std::shared_ptr<Texture> wrap(std::shared_ptr<IImage>     img,
                                         std::shared_ptr<IImageView> view,
                                         const std::string&          label = "");

  private:
    // Default constructor for factory use
    Texture() = default;

    // clang-format off
  private: struct dummy {};
  public: Texture(dummy d) {}
    // clang-format on

  private:
    static std::shared_ptr<Texture> createShared() { return ya::make_shared<Texture>(dummy{}); }

    // Internal initialization methods (called by factory)
    void initFromData(IRender& render,
                      const void* pixels,
                      size_t dataSize,
                      uint32_t texWidth,
                      uint32_t texHeight,
                      EFormat::T format,
                      uint32_t mipLevels = 1,
                      bool generateMipmaps = false);
    void initCubeMapFromMemory(IRender& render, const CubeMapMemoryCreateInfo& ci);
    void initFallbackTexture(IRender& render, const void* pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight);

  public:
    virtual ~Texture() = default;

    // Disable copy, allow move
    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&)                 = default;
    Texture& operator=(Texture&&)      = default;

    // Platform-independent accessors
    IImage*                 getImage() const { return image.get(); }
    std::shared_ptr<IImage> getImageShared() const { return image; }
    IImageView*             getImageView() const { return imageView.get(); }
    std::shared_ptr<IImageView> getImageViewShared() const { return imageView; }
    const std::vector<std::shared_ptr<void>>& getRetainedResources() const { return retainedResources; }

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    uint32_t   getChannels() const { return _channels; }
    EFormat::T getFormat() const { return _format; }

    void               setLabel(const std::string& label);
    const std::string& getLabel() const { return _label; }
    const std::string& getFilepath() const { return _filepath; }
    Extent2D           getExtent() const { return Extent2D{.width = _width, .height = _height}; }

    bool isValid() const { return image && imageView && _width > 0 && _height > 0; }

};

/**
 * @brief Lightweight runtime texture binding - replaces TextureView
 *
 * Holds only the resolved resource pointers (Texture + Sampler).
 * Does NOT store bEnable (that belongs in the param UBO).
 * Provides convenience accessors that return GPU Handles with fallback.
 */
struct YA_RHI_API TextureBinding
{
    ya::Ptr<Texture> texture = nullptr;
    ya::Ptr<Sampler> sampler = nullptr;

    [[nodiscard]] bool isValid() const { return texture && sampler; }

    /**
     * @brief Get the GPU image-view handle, falling back to white texture if unset.
     */
    [[nodiscard]] ImageViewHandle getImageViewHandle() const;

    /**
     * @brief Get the GPU sampler handle, falling back to default sampler if unset.
     */
    [[nodiscard]] SamplerHandle getSamplerHandle() const;

    // Convenience accessors
    Texture* getTexture() const { return texture.get(); }
    Sampler* getSampler() const { return sampler.get(); }

    void setTexture(ya::Ptr<Texture> tex) { texture = std::move(tex); }
    void setSampler(ya::Ptr<Sampler> smp) { sampler = std::move(smp); }

    void clear()
    {
        texture = nullptr;
        sampler = nullptr;
    }
};
} // namespace ya
