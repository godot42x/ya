#pragma once


#include "Core/Base.h"
#include "Core/Delegate.h"
#include "Image.h"
#include "Render/RenderDefines.h"
#include "Sampler.h"


namespace ya
{

template <typename ComponentType>
struct ColorRGBA
{
    using ty = ComponentType;
    ty r;
    ty g;
    ty b;
    ty a;
};

using ColorU8_t = ColorRGBA<uint8_t>;

/**
 * @brief Texture - Platform-independent texture wrapper
 *
 * Manages image data, image view, and metadata.
 * Can be created from file or raw pixel data.
 */
struct Texture
{
    EFormat::T _format   = EFormat::R8G8B8A8_UNORM;
    uint32_t   _width    = 0;
    uint32_t   _height   = 0;
    uint32_t   _channels = 4; // RGBA by default

    std::string _label;
    std::string _filepath;

    std::shared_ptr<IImage>     image;
    std::shared_ptr<IImageView> imageView;


    /**
     * @brief Create texture from file
     */
    Texture(const std::string &filepath);

    /**
     * @brief Create texture from raw RGBA8 data
     */
    Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data);

    /**
     * @brief Create texture from raw data with custom format
     */
    Texture(uint32_t width, uint32_t height, const void *data, size_t dataSize, EFormat::T format);

    virtual ~Texture() = default;

    // Disable copy, allow move
    Texture(const Texture &)            = delete;
    Texture &operator=(const Texture &) = delete;
    Texture(Texture &&)                 = default;
    Texture &operator=(Texture &&)      = default;

    // Platform-independent accessors (preferred)
    ImageHandle                 getImage() const { return image ? image->getHandle() : ImageHandle{}; }
    std::shared_ptr<IImageView> getImageView() const { return imageView; }
    ImageViewHandle             getImageViewHandle() const { return imageView ? imageView->getHandle() : ImageViewHandle{}; }
    FormatHandle                getFormatHandle() const;

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    uint32_t   getChannels() const { return _channels; }
    EFormat::T getFormat() const { return _format; }

    void setLabel(const std::string &label);
    const std::string &getLabel() const { return _label; }
    const std::string &getFilepath() const { return _filepath; }

    /**
     * @brief Check if texture is valid
     */
    bool isValid() const { return image && imageView && _width > 0 && _height > 0; }

  private:

    void createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight, EFormat::T format = EFormat::R8G8B8A8_UNORM);


  private:
};


struct TextureView
{
    stdptr<Texture> texture = nullptr;
    stdptr<Sampler> sampler = nullptr;

    bool      bEnable = true;
    glm::vec2 uvTranslation{0.f};
    glm::vec2 uvScale{1.0f};
    float     uvRotation = 0.f;

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

    Texture *getTexture() const { return texture.get(); }
    void     setTexture(const stdptr<Texture> &texture_) { texture = texture_; }

    Sampler *getSampler() const { return sampler.get(); }
    void     setSampler(const stdptr<Sampler> &sampler_) { sampler = sampler_; }

    TextureView *setEnable(bool bEnable_)
    {
        bEnable = bEnable_;
        return this;
    }
    TextureView *setUvTranslation(const glm::vec2 &uvTranslation_)
    {
        uvTranslation = uvTranslation_;
        return this;
    }
    TextureView *setUvScale(const glm::vec2 &uvScale_)
    {
        uvScale = uvScale_;
        return this;
    }
    TextureView *setUvRotation(float uvRotation_)
    {
        uvRotation = uvRotation_;
        return this;
    }
};



} // namespace ya