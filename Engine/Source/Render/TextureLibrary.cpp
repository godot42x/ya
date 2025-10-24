#include "TextureLibrary.h"
#include "Core/Log.h"

namespace ya
{

// Static member initialization
std::shared_ptr<Texture> TextureLibrary::s_whiteTexture;
std::shared_ptr<Texture> TextureLibrary::s_blackTexture;
std::shared_ptr<Texture> TextureLibrary::s_multiPixelTexture;

std::shared_ptr<Sampler> TextureLibrary::s_defaultSampler;
std::shared_ptr<Sampler> TextureLibrary::s_linearSampler;
std::shared_ptr<Sampler> TextureLibrary::s_nearestSampler;

bool TextureLibrary::s_initialized = false;

void TextureLibrary::init()
{
    if (s_initialized) {
        YA_CORE_WARN("TextureLibrary already initialized");
        return;
    }

    createSamplers();
    createTextures();

    s_initialized = true;
    YA_CORE_INFO("TextureLibrary initialized");
}

void TextureLibrary::destroy()
{
    if (!s_initialized) {
        return;
    }

    // Cleanup textures
    s_whiteTexture.reset();
    s_blackTexture.reset();
    s_multiPixelTexture.reset();

    // Cleanup samplers
    s_defaultSampler.reset();
    s_linearSampler.reset();
    s_nearestSampler.reset();

    s_initialized = false;
    YA_CORE_INFO("TextureLibrary destroyed");
}

void TextureLibrary::createSamplers()
{
    s_linearSampler = Sampler::create(
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

    s_nearestSampler = Sampler::create(
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

    s_defaultSampler = s_linearSampler;
}

void TextureLibrary::createTextures()
{
    using color_t = ColorRGBA<uint8_t>;
    
    color_t white{.r = 255, .g = 255, .b = 255, .a = 255};
    color_t black{.r = 0, .g = 0, .b = 0, .a = 255};
    color_t blue{.r = 0, .g = 0, .b = 255, .a = 255};

    // Create 1x1 white texture
    s_whiteTexture = makeShared<Texture>(1, 1, std::vector<color_t>{white});
    s_whiteTexture->setLabel("white");

    // Create 1x1 black texture
    s_blackTexture = makeShared<Texture>(1, 1, std::vector<color_t>{black});
    s_blackTexture->setLabel("black");

    // Create 2x2 multi-pixel texture with pattern: white, blue, blue, white
    s_multiPixelTexture = makeShared<Texture>(2, 2, std::vector<color_t>{
        white, blue,
        blue, white,
    });
    s_multiPixelTexture->setLabel("multi-pixel");
}

std::shared_ptr<Texture> TextureLibrary::getWhiteTexture()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_whiteTexture;
}

std::shared_ptr<Texture> TextureLibrary::getBlackTexture()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_blackTexture;
}

std::shared_ptr<Texture> TextureLibrary::getMultiPixelTexture()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_multiPixelTexture;
}

std::shared_ptr<Sampler> TextureLibrary::getDefaultSampler()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_defaultSampler;
}

std::shared_ptr<Sampler> TextureLibrary::getLinearSampler()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_linearSampler;
}

std::shared_ptr<Sampler> TextureLibrary::getNearestSampler()
{
    YA_CORE_ASSERT(s_initialized, "TextureLibrary not initialized");
    return s_nearestSampler;
}

} // namespace ya
