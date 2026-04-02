#pragma once

#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Image.h"
#include "Render/RenderDefines.h"
#include "Sampler.h"
#include "TextureFactory.h"

#include <array>

namespace ya
{

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

/**
 * @brief Intermediate container carrying decoded pixel data between threads.
 *
 * Worker thread:  file IO + stbi_load() → fills this struct (CPU only)
 * Main thread:    fromDecodedData() → creates VkImage, staging buffer, GPU upload
 *
 * The pixel buffer uses shared_ptr with a custom deleter (stbi_image_free)
 * so it's safe to move between threads and auto-cleans on destruction.
 */
struct DecodedTextureData
{
    std::string             filepath;
    std::string             label;
    uint32_t                width    = 0;
    uint32_t                height   = 0;
    uint32_t                channels = 4; // Always RGBA after stbi_load with STBI_rgb_alpha
    EFormat::T              format   = EFormat::R8G8B8A8_UNORM;
    std::shared_ptr<uint8_t> pixels;      // Decoded RGBA pixel data (stbi_image_free deleter)

    [[nodiscard]] bool isValid() const { return pixels && width > 0 && height > 0; }
    [[nodiscard]] size_t dataSize() const { return static_cast<size_t>(width) * height * channels; }

    /**
     * @brief Decode a texture file on the current thread (CPU only, no GPU).
     *        Thread-safe: uses no GPU resources.
     * @param filepath  Path to the image file.
     * @param label     Optional debug label.
     * @param bSRGB     If true, format is R8G8B8A8_SRGB; otherwise R8G8B8A8_UNORM.
     * @return Decoded data, or invalid struct on failure.
     */
    static DecodedTextureData decode(const std::string& filepath,
                                     const std::string& label = "",
                                     bool bSRGB = true);
};

struct Texture
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

  public:

    static std::shared_ptr<Texture> fromFile(const std::string& filepath, const std::string& label = "", bool bSRGB = true);

    /**
     * @brief Create a GPU texture from pre-decoded pixel data.
     *        MUST be called on the main/render thread (creates VkImage, uploads via staging buffer).
     *
     * This is the second half of the async pipeline:
     *   Worker thread:  DecodedTextureData::decode() → DecodedTextureData
     *   Main thread:    Texture::fromDecodedData(decoded) → shared_ptr<Texture>
     */
    static std::shared_ptr<Texture> fromDecodedData(DecodedTextureData&& decoded);

    static std::shared_ptr<Texture> fromData(uint32_t                               width,
                                             uint32_t                               height,
                                             const std::vector<ColorRGBA<uint8_t>>& data,
                                             const std::string&                     label = "");

    static std::shared_ptr<Texture> fromData(uint32_t           width,
                                             uint32_t           height,
                                             const void*        data,
                                             size_t             dataSize,
                                             EFormat::T         format,
                                             const std::string& label = "");

    static std::shared_ptr<Texture> createCubeMap(const CubeMapCreateInfo& ci);
    static std::shared_ptr<Texture> createSolidCubeMap(const ColorU8_t& color,
                               const std::string& label = "");

    static std::shared_ptr<Texture> createRenderTexture(const RenderTextureCreateInfo& ci);

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
    friend struct ITextureFactory;
    friend class VulkanTextureFactory;

    // Default constructor for factory use
    Texture() = default;

    // clang-format off
  private: struct dummy {};
  public: Texture(dummy d) {}
    // clang-format on

  private:
    static std::shared_ptr<Texture> createShared() { return ya::make_shared<Texture>(dummy{}); }

    // Internal initialization methods (called by factory)
    void initFromData(const void* pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight, EFormat::T format, uint32_t mipLevels = 1);
    void initCubeMap(const CubeMapCreateInfo& ci);
    void initFallbackTexture(const void* pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight);

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

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    uint32_t   getChannels() const { return _channels; }
    EFormat::T getFormat() const { return _format; }

    void               setLabel(const std::string& label);
    const std::string& getLabel() const { return _label; }
    const std::string& getFilepath() const { return _filepath; }
    Extent2D           getExtent() const { return Extent2D{.width = _width, .height = _height}; }

    bool isValid() const { return image && imageView && _width > 0 && _height > 0; }

  private:
    /**
     * @brief Get texture factory
     * @return ITextureFactory interface pointer
     * @throw If factory is not available
     */
    static struct ITextureFactory* getTextureFactory();
};


/**
 * @brief Lightweight runtime texture binding — replaces TextureView
 *
 * Holds only the resolved resource pointers (Texture + Sampler).
 * Does NOT store bEnable (that belongs in the param UBO).
 * Provides convenience accessors that return GPU Handles with fallback.
 */
struct TextureBinding
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