#include "TextureLibrary.h"
#include "Core/Log.h"

namespace ya
{

TextureLibrary &TextureLibrary::get()
{
    static TextureLibrary instance;
    return instance;
}

void TextureLibrary::init()
{
    if (_initialized) {
        YA_CORE_WARN("TextureLibrary already initialized");
        return;
    }

    createSamplers();
    createTextures();

    _initialized = true;
    YA_CORE_INFO("TextureLibrary initialized");
}

void TextureLibrary::clearCache()
{
    if (!_initialized) {
        return;
    }

    // Cleanup textures
    _whiteTexture.reset();
    _blackTexture.reset();
    _multiPixelTexture.reset();

    // Cleanup samplers
    _defaultSampler.reset();
    _linearSampler.reset();
    _nearestSampler.reset();

    _initialized = false;
    YA_CORE_INFO("TextureLibrary cleared");
}

void TextureLibrary::createSamplers()
{
    _linearSampler = Sampler::create(
        SamplerDesc{
            .label         = "linear",
            .minFilter     = EFilter::Linear,
            .magFilter     = EFilter::Linear,
            .mipmapMode    = ESamplerMipmapMode::Linear,
            .addressModeU  = ESamplerAddressMode::Repeat,
            .addressModeV  = ESamplerAddressMode::Repeat,
            .addressModeW  = ESamplerAddressMode::Repeat,
            .mipLodBias    = 0.0f,
            .maxAnisotropy = 1.0f,
        });

    _nearestSampler = Sampler::create(
        SamplerDesc{
            .label         = "nearest",
            .minFilter     = EFilter::Nearest,
            .magFilter     = EFilter::Nearest,
            .mipmapMode    = ESamplerMipmapMode::Nearest,
            .addressModeU  = ESamplerAddressMode::Repeat,
            .addressModeV  = ESamplerAddressMode::Repeat,
            .addressModeW  = ESamplerAddressMode::Repeat,
            .mipLodBias    = 0.0f,
            .maxAnisotropy = 1.0f,
        });

    _defaultSampler = _linearSampler;
}

void TextureLibrary::createTextures()
{
    using color_t = ColorRGBA<uint8_t>;

    color_t white{.r = 255, .g = 255, .b = 255, .a = 255};
    color_t black{.r = 0, .g = 0, .b = 0, .a = 255};
    color_t blue{.r = 0, .g = 0, .b = 255, .a = 255};

    // Create 1x1 white texture
    _whiteTexture = Texture::fromData(1, 1, std::vector<color_t>{white}, "white");

    // Create 1x1 black texture
    _blackTexture = Texture::fromData(1, 1, std::vector<color_t>{black}, "black");

    // Create 2x2 multi-pixel texture with pattern: white, blue, blue, white
    _multiPixelTexture = Texture::fromData(2, 2, std::vector<color_t>{
                                                       white,
                                                       blue,
                                                       blue,
                                                       white,
                                                   }, "multi-pixel");
}

ya::Ptr<Texture> TextureLibrary::getWhiteTexture()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Texture>(_whiteTexture);
}

ya::Ptr<Texture> TextureLibrary::getBlackTexture()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Texture>(_blackTexture);
}

ya::Ptr<Texture> TextureLibrary::getMultiPixelTexture()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Texture>(_multiPixelTexture);
}

ya::Ptr<Sampler> TextureLibrary::getDefaultSampler()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Sampler>(_defaultSampler);
}

ya::Ptr<Sampler> TextureLibrary::getLinearSampler()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Sampler>(_linearSampler);
}

ya::Ptr<Sampler> TextureLibrary::getNearestSampler()
{
    YA_CORE_ASSERT(_initialized, "TextureLibrary not initialized");
    return ya::Ptr<Sampler>(_nearestSampler);
}

} // namespace ya
