#pragma once


#include "Core/Base.h"
#include "Image.h"
#include "Sampler.h"
#include "Render/RenderDefines.h"

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

struct Texture
{
    EFormat::T _format;
    uint32_t   _width  = 0;
    uint32_t   _height = 0;

    std::string _label;
    std::string _filepath;

    std::shared_ptr<IImage>     image;
    std::shared_ptr<IImageView> imageView;


    Texture(const std::string &filepath);
    Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data);

    virtual ~Texture() = default;

    // Platform-independent accessors (preferred)
    ImageHandle     getImage() const { return image ? image->getHandle() : ImageHandle{}; }
    ImageViewHandle getImageView() const { return imageView ? imageView->getHandle() : ImageViewHandle{}; }
    FormatHandle    getFormatHandle() const;

    uint32_t   getWidth() const { return _width; }
    uint32_t   getHeight() const { return _height; }
    EFormat::T getFormat() const { return _format; }

    void               setLabel(const std::string &label) { _label = label; }
    const std::string &getLabel() const { return _label; }
    const std::string &getFilepath() const { return _filepath; }

  private:

    void createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight);


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
    Sampler *getSampler() const { return sampler.get(); }
};


} // namespace ya