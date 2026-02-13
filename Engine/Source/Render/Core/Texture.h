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

    static std::shared_ptr<Texture> fromFile(const std::string& filepath, const std::string& label = "");

    static std::shared_ptr<Texture> fromData(
        uint32_t                               width,
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
    static std::shared_ptr<Texture> wrap(
        std::shared_ptr<IImage>     img,
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
    IImage*     getImage() const { return image.get(); }
    IImageView* getImageView() const { return imageView.get(); }

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    uint32_t   getChannels() const { return _channels; }
    EFormat::T getFormat() const { return _format; }

    void               setLabel(const std::string& label);
    const std::string& getLabel() const { return _label; }
    const std::string& getFilepath() const { return _filepath; }
    Extent2D           getExtent() const { return Extent2D{.width = _width, .height = _height}; }

    /**
     * @brief Check if texture is valid
     */
    bool isValid() const { return image && imageView && _width > 0 && _height > 0; }

    /**
     * @brief Get texture render API type
     * @return Render API type
     */
    ERenderAPI::T getRenderAPI() const;

  private:
    /**
     * @brief Get texture factory
     * @return ITextureFactory interface pointer
     * @throw If factory is not available
     */
    static class ITextureFactory* getTextureFactory();
};


struct TextureView
{
    YA_REFLECT_BEGIN(TextureView)
    YA_REFLECT_FIELD(texture)
    YA_REFLECT_FIELD(sampler)
    YA_REFLECT_FIELD(bEnable)
    YA_REFLECT_END()

    ya::Ptr<Texture> texture = nullptr;
    ya::Ptr<Sampler> sampler = nullptr;

    bool bEnable = true;

    static TextureView create(stdptr<Texture> texture, stdptr<Sampler> sampler)
    {
        return TextureView{
            .texture = texture,
            .sampler = sampler,
        };
    }

    [[nodiscard]] bool isValid() const
    {
        return texture && sampler;
    }

    Texture* getTexture() const { return texture.get(); }
    void     setTexture(const stdptr<Texture>& texture_) { texture = texture_; }

    Sampler* getSampler() const { return sampler.get(); }
    void     setSampler(const stdptr<Sampler>& sampler_) { sampler = sampler_; }

    TextureView* setEnable(bool bEnable_)
    {
        bEnable = bEnable_;
        return this;
    }
};

} // namespace ya