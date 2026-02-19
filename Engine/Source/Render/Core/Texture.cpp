#include "Texture.h"

#include "Core/App/App.h"
#include "stb/stb_image.h"

#include <cstddef>
#include <cstring>

#include "Render/Core/CommandBuffer.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/Image.h" // Include Image.h for RHI enums
#include "Render/Core/TextureFactory.h"
#include "Render/Render.h"

#include "ktx.h"


namespace ya
{

namespace
{
struct StbiImage
{
    std::shared_ptr<stbi_uc> data = nullptr;

    StbiImage() = default;
    explicit StbiImage(stbi_uc *ptr)
    {
        data = std::shared_ptr<stbi_uc>(ptr, [](stbi_uc *ptr) {
            stbi_image_free(ptr);
        });
    }
    ~StbiImage()
    {
    }
    operator bool() const { return data != nullptr; }
    const stbi_uc *get() const { return data.get(); }
    stbi_uc       *get() { return data.get(); }
};

class StbiFlipGuard
{
  public:
    explicit StbiFlipGuard(bool flip) : m_flip(flip)
    {
        if (m_flip) {
            stbi_set_flip_vertically_on_load_thread(true);
        }
    }
    ~StbiFlipGuard()
    {
        if (m_flip) {
            stbi_set_flip_vertically_on_load_thread(false);
        }
    }
    bool m_flip = false;
};
} // namespace

static size_t getFormatPixelSize(EFormat::T format);
static bool   isBlockCompressed(EFormat::T format);

/**
 * @brief Get the size in bytes per pixel/block for a given format
 */
static size_t getFormatPixelSize(EFormat::T format)
{
    switch (format) {
    case EFormat::R8_UNORM:
        return 1;
    case EFormat::R8G8_UNORM:
        return 2;
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::R8G8B8A8_SRGB:
    case EFormat::B8G8R8A8_UNORM:
    case EFormat::B8G8R8A8_SRGB:
    case EFormat::D32_SFLOAT:
    case EFormat::D24_UNORM_S8_UINT:
        return 4;
    case EFormat::D32_SFLOAT_S8_UINT:
        return 5; // 4 bytes for depth + 1 byte for stencil (padded to 8 in practice)

    // BC formats (block size)
    case EFormat::BC1_RGB_UNORM_BLOCK:
    case EFormat::BC1_RGBA_UNORM_BLOCK:
    case EFormat::BC1_RGB_SRGB_BLOCK:
    case EFormat::BC1_RGBA_SRGB_BLOCK:
    case EFormat::BC4_UNORM_BLOCK:
    case EFormat::BC4_SNORM_BLOCK:
        return 8; // 8 bytes per 4x4 block
    case EFormat::BC3_UNORM_BLOCK:
    case EFormat::BC3_SRGB_BLOCK:
    case EFormat::BC5_UNORM_BLOCK:
    case EFormat::BC5_SNORM_BLOCK:
    case EFormat::BC7_UNORM_BLOCK:
    case EFormat::BC7_SRGB_BLOCK:
        return 16; // 16 bytes per 4x4 block

    // ASTC formats (all 16 bytes per block)
    case EFormat::ASTC_4x4_UNORM_BLOCK:
    case EFormat::ASTC_4x4_SRGB_BLOCK:
    case EFormat::ASTC_5x5_UNORM_BLOCK:
    case EFormat::ASTC_5x5_SRGB_BLOCK:
    case EFormat::ASTC_6x6_UNORM_BLOCK:
    case EFormat::ASTC_6x6_SRGB_BLOCK:
    case EFormat::ASTC_8x8_UNORM_BLOCK:
    case EFormat::ASTC_8x8_SRGB_BLOCK:
    case EFormat::ASTC_10x10_UNORM_BLOCK:
    case EFormat::ASTC_10x10_SRGB_BLOCK:
        return 16;

    // ETC2 formats
    case EFormat::ETC2_R8G8B8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8A1_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A1_SRGB_BLOCK:
        return 8;
    case EFormat::ETC2_R8G8B8A8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A8_SRGB_BLOCK:
        return 16;

    default:
        YA_CORE_WARN("Unknown format pixel size for format: {}", static_cast<int>(format));
        return 4; // Default to 4 bytes
    }
}

/**
 * @brief Check if a format is block compressed
 */
static bool isBlockCompressed(EFormat::T format)
{
    switch (format) {
    case EFormat::BC1_RGB_UNORM_BLOCK:
    case EFormat::BC1_RGBA_UNORM_BLOCK:
    case EFormat::BC1_RGB_SRGB_BLOCK:
    case EFormat::BC1_RGBA_SRGB_BLOCK:
    case EFormat::BC3_UNORM_BLOCK:
    case EFormat::BC3_SRGB_BLOCK:
    case EFormat::BC4_UNORM_BLOCK:
    case EFormat::BC4_SNORM_BLOCK:
    case EFormat::BC5_UNORM_BLOCK:
    case EFormat::BC5_SNORM_BLOCK:
    case EFormat::BC7_UNORM_BLOCK:
    case EFormat::BC7_SRGB_BLOCK:
    case EFormat::ASTC_4x4_UNORM_BLOCK:
    case EFormat::ASTC_4x4_SRGB_BLOCK:
    case EFormat::ASTC_5x5_UNORM_BLOCK:
    case EFormat::ASTC_5x5_SRGB_BLOCK:
    case EFormat::ASTC_6x6_UNORM_BLOCK:
    case EFormat::ASTC_6x6_SRGB_BLOCK:
    case EFormat::ASTC_8x8_UNORM_BLOCK:
    case EFormat::ASTC_8x8_SRGB_BLOCK:
    case EFormat::ASTC_10x10_UNORM_BLOCK:
    case EFormat::ASTC_10x10_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8A1_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A1_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8A8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A8_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

// ====== Static Factory Methods Implementation ======

ITextureFactory *Texture::getTextureFactory()
{
    auto render = ya::App::get()->getRender();
    YA_CORE_ASSERT(render, "Render is not initialized");

    auto textureFactory = render->getTextureFactory();
    YA_CORE_ASSERT(textureFactory && textureFactory->isValid(), "TextureFactory is not available");

    return textureFactory;
}

std::shared_ptr<Texture> Texture::fromFile(const std::string& filepath, const std::string& label, bool bSRGB)
{
    stdpath p = filepath;
    if (p.extension() == ".ktx" || p.extension() == ".ktx2") {
        // TODO: KTX texture loading support
        YA_CORE_WARN("KTX texture loading not yet implemented: {}", filepath);
        return nullptr;
    }

    int       texWidth = -1, texHeight = -1, texChannels = -1;
    StbiImage pixels(stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha));
    if (!pixels) {
        YA_CORE_ERROR("Failed to load texture image: {}", filepath);
        return nullptr;
    }

    auto texture       = Texture::createShared();
    texture->_filepath = filepath;
    texture->_label    = label.empty() ? filepath : label;
    texture->_channels = 4; // Force RGBA
    texture->initFromData(pixels.get(),
                          0,
                          (uint32_t)texWidth,
                          (uint32_t)texHeight,
                          bSRGB ? EFormat::R8G8B8A8_SRGB : EFormat::R8G8B8A8_UNORM);

    YA_CORE_TRACE("Created texture from file: {} ({}x{}, {} channels)", filepath, texWidth, texHeight, texChannels);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(
    uint32_t                               width,
    uint32_t                               height,
    const std::vector<ColorRGBA<uint8_t>> &data,
    const std::string                     &label)
{
    YA_CORE_ASSERT((uint32_t)data.size() == width * height, "Pixel data size does not match width * height");

    auto texture       = Texture::createShared();
    texture->_label    = label;
    texture->_channels = 4;
    texture->initFromData(data.data(), 0, width, height, EFormat::R8G8B8A8_UNORM);

    YA_CORE_TRACE("Created texture from RGBA data ({}x{}) label: {}", width, height, label);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(
    uint32_t           width,
    uint32_t           height,
    const void        *data,
    size_t             dataSize,
    EFormat::T         format,
    const std::string &label)
{
    auto texture     = Texture::createShared();
    texture->_label  = label;
    texture->_format = format;

    // Derive channels from format
    switch (format) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::B8G8R8A8_UNORM:
        texture->_channels = 4;
        break;
    default:
        texture->_channels = 4; // Default
        break;
    }

    texture->initFromData(data, dataSize, width, height, format, 1);

    YA_CORE_TRACE("Created texture from raw data ({}x{}, format: {}) label: {}", width, height, (int)format, label);
    return texture;
}

std::shared_ptr<Texture> Texture::createCubeMap(const CubeMapCreateInfo &ci)
{
    auto texture    = Texture::createShared();
    texture->_label = ci.label;
    texture->initCubeMap(ci);

    if (!texture->isValid()) {
        return nullptr;
    }

    return texture;
}

std::shared_ptr<Texture> Texture::createRenderTexture(const RenderTextureCreateInfo &ci)
{
    auto textureFactory = getTextureFactory();

    // Create image for render target
    ImageCreateInfo imageCI{
        .label  = ci.label,
        .format = ci.format,
        .extent = {
            .width  = ci.width,
            .height = ci.height,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .samples       = ci.samples,
        .usage         = ci.usage,
        .initialLayout = EImageLayout::Undefined,
    };

    auto image = textureFactory->createImage(imageCI);
    if (!image) {
        YA_CORE_ERROR("Failed to create render target image: {}", ci.label);
        return nullptr;
    }

    // Create image view
    uint32_t aspectFlags = ci.isDepth ? EImageAspect::Depth : EImageAspect::Color;
    auto     imageView   = textureFactory->createImageView(image, aspectFlags);
    if (!imageView) {
        YA_CORE_ERROR("Failed to create render target image view: {}", ci.label);
        return nullptr;
    }

    return Texture::wrap(image, imageView, ci.label);
}

std::shared_ptr<Texture> Texture::wrap(std::shared_ptr<IImage>     img,
                                       std::shared_ptr<IImageView> view,
                                       const std::string          &label)
{
    YA_CORE_ASSERT(img && view, "Cannot wrap null IImage or IImageView");

    auto texture = createShared();

    texture->_label    = label;
    texture->image     = img;
    texture->imageView = view;

    // Extract metadata from the wrapped image using RHI interface
    texture->_width     = img->getWidth();
    texture->_height    = img->getHeight();
    texture->_format    = img->getFormat();
    texture->_mipLevels = 1; // Default mip levels
    texture->_channels  = 4; // Default

    YA_CORE_TRACE("Created Texture from existing IImage/IImageView: {} ({}x{})", label, texture->_width, texture->_height);
    return texture;
}

void Texture::setLabel(const std::string &label)
{
    _label = label;
    if (image) {
        image->setDebugName(std::format("Texture_Image_{}", label));
    }
    if (imageView) {
        imageView->setDebugName(std::format("Texture_ImageView_{}", label));
    }
}

void Texture::initFromData(const void *pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight, EFormat::T format, uint32_t mipLevels)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = format;
    _mipLevels = mipLevels;

    auto render         = App::get()->getRender();
    auto textureFactory = render->getTextureFactory();

    // Calculate image size
    VkDeviceSize imageSize = 0;
    if (dataSize > 0) {
        imageSize = dataSize;
    }
    else {
        size_t pixelSize = getFormatPixelSize(format);
        imageSize        = pixelSize * texWidth * texHeight;
    }

    auto ci = ya::ImageCreateInfo{
        .label  = std::format("Texture_Image_{}", _label),
        .format = format,
        .extent = {
            .width  = texWidth,
            .height = texHeight,
            .depth  = 1,
        },
        .mipLevels     = mipLevels,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
        .initialLayout = EImageLayout::Undefined,
    };

    // Create image using texture factory
    image = textureFactory->createImage(ci);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create image for texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      (int)format,
                      texWidth,
                      texHeight);

        // Create a fallback 1x1 magenta texture to indicate error
        uint32_t magentaPixel = 0xFFFF00FF; // RGBA: magenta
        initFallbackTexture(&magentaPixel, sizeof(magentaPixel), 1, 1);
        return;
    }

    // Create image view using texture factory
    imageView = textureFactory->createImageView(image, EImageAspect::Color);
    YA_CORE_ASSERT(imageView, "Failed to create image view for texture: {} (format: {}, {}x{})", _filepath.empty() ? _label : _filepath, (int)format, texWidth, texHeight);

    // Create staging buffer
    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = std::format("StagingBuffer_Texture_{}", _filepath.empty() ? _label : _filepath),
            .usage         = EBufferUsage::TransferSrc,
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(imageSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    auto *cmdBuf = render->beginIsolateCommands();

    // Transition image layout: UNDEFINED -> TRANSFER_DST
    cmdBuf->transitionImageLayout(image.get(), EImageLayout::Undefined, EImageLayout::TransferDst);

    // For block compressed formats with multiple mip levels
    bool isCompressed = isBlockCompressed(format);

    if (isCompressed && mipLevels > 1 && dataSize > 0) {
        VkDeviceSize bufferOffset  = 0;
        uint32_t     currentWidth  = texWidth;
        uint32_t     currentHeight = texHeight;

        for (uint32_t level = 0; level < mipLevels; level++) {
            currentWidth  = std::max(currentWidth, 1u);
            currentHeight = std::max(currentHeight, 1u);

            uint32_t     blockSize = getFormatPixelSize(format);
            uint32_t     blocksX   = (currentWidth + 3) / 4;
            uint32_t     blocksY   = (currentHeight + 3) / 4;
            VkDeviceSize levelSize = blocksX * blocksY * blockSize;

            if (bufferOffset + levelSize > imageSize) {
                YA_CORE_ERROR("Mip level {} data exceeds buffer size: {} > {}",
                              level,
                              bufferOffset + levelSize,
                              imageSize);
                break;
            }

            BufferImageCopy region{
                .bufferOffset      = bufferOffset,
                .bufferRowLength   = 0,
                .bufferImageHeight = 0,
                .imageSubresource  = {
                     .aspectMask     = EImageAspect::Color,
                     .mipLevel       = level,
                     .baseArrayLayer = 0,
                     .layerCount     = 1,
                },
                .imageOffsetX      = 0,
                .imageOffsetY      = 0,
                .imageOffsetZ      = 0,
                .imageExtentWidth  = currentWidth,
                .imageExtentHeight = currentHeight,
                .imageExtentDepth  = 1,
            };

            cmdBuf->copyBufferToImage(stagingBuffer.get(), image.get(), EImageLayout::TransferDst, {region});

            bufferOffset += levelSize;
            currentWidth /= 2;
            currentHeight /= 2;
        }
    }
    else {
        BufferImageCopy region{
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                 .aspectMask     = EImageAspect::Color,
                 .mipLevel       = 0,
                 .baseArrayLayer = 0,
                 .layerCount     = 1,
            },
            .imageOffsetX      = 0,
            .imageOffsetY      = 0,
            .imageOffsetZ      = 0,
            .imageExtentWidth  = texWidth,
            .imageExtentHeight = texHeight,
            .imageExtentDepth  = 1,
        };

        cmdBuf->copyBufferToImage(stagingBuffer.get(), image.get(), EImageLayout::TransferDst, {region});
    }

    // Transition to shader read-only layout
    cmdBuf->transitionImageLayout(image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal);

    render->endIsolateCommands(cmdBuf);
}

void Texture::initFallbackTexture(const void *pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = EFormat::R8G8B8A8_UNORM;
    _mipLevels = 1;
    _channels  = 4;

    auto textureFactory = getTextureFactory();
    auto render         = textureFactory->getRender();

    auto ci = ya::ImageCreateInfo{
        .label  = std::format("Texture_Fallback_{}", _label),
        .format = EFormat::R8G8B8A8_UNORM,
        .extent = {
            .width  = texWidth,
            .height = texHeight,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
        .initialLayout = EImageLayout::Undefined,
    };

    image = textureFactory->createImage(ci);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create fallback texture!");
        return;
    }

    imageView = textureFactory->createImageView(image, EImageAspect::Color);
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create fallback texture image view!");
        image = nullptr;
        return;
    }

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label         = std::format("StagingBuffer_Fallback_{}", _label),
            .usage         = EBufferUsage::TransferSrc,
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(dataSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    auto *cmdBuf = render->beginIsolateCommands();

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::Undefined, EImageLayout::TransferDst);

    BufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
             .aspectMask     = EImageAspect::Color,
             .mipLevel       = 0,
             .baseArrayLayer = 0,
             .layerCount     = 1,
        },
        .imageOffsetX      = 0,
        .imageOffsetY      = 0,
        .imageOffsetZ      = 0,
        .imageExtentWidth  = texWidth,
        .imageExtentHeight = texHeight,
        .imageExtentDepth  = 1,
    };
    cmdBuf->copyBufferToImage(stagingBuffer.get(), image.get(), EImageLayout::TransferDst, {region});

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal);

    render->endIsolateCommands(cmdBuf);

    YA_CORE_WARN("Created fallback texture ({}x{}) for: {}", texWidth, texHeight, _filepath.empty() ? _label : _filepath);
}

void Texture::initCubeMap(const CubeMapCreateInfo &ci)
{
    auto textureFactory = getTextureFactory();
    auto render         = textureFactory->getRender();

    std::array<StbiImage, CubeFace_Count> pixels{};

    int width    = 0;
    int height   = 0;
    int channels = 0;

    StbiFlipGuard flipGuard(ci.flipVertical);
    for (size_t i = 0; i < CubeFace_Count; ++i) {
        pixels[i] = StbiImage(stbi_load(ci.files[i].c_str(), &width, &height, &channels, STBI_rgb_alpha));
        if (!pixels[i]) {
            YA_CORE_ERROR("Failed to load cubemap face {}: {}", i, ci.files[i]);
            return;
        }
        if (i == 0) {
            _width    = static_cast<uint32_t>(width);
            _height   = static_cast<uint32_t>(height);
            _channels = 4;
        }
        if (_width != static_cast<uint32_t>(width) || _height != static_cast<uint32_t>(height)) {
            YA_CORE_ERROR("Cubemap faces must have the same dimensions");
            return;
        }
    }

    _format    = EFormat::R8G8B8A8_UNORM;
    _mipLevels = 1;

    VkDeviceSize faceSize  = static_cast<VkDeviceSize>(_width) * static_cast<VkDeviceSize>(_height) * 4;
    VkDeviceSize totalSize = faceSize * CubeFace_Count;

    ImageCreateInfo imageCI{
        .label  = std::format("CubeMap_{}", _label),
        .format = _format,
        .extent = {
            .width  = _width,
            .height = _height,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = CubeFace_Count,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
        .initialLayout = EImageLayout::Undefined,
        .flags         = EImageCreateFlag::CubeCompatible,
    };

    image = textureFactory->createImage(imageCI);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create cubemap image: {}", _label);
        return;
    }

    imageView = textureFactory->createCubeMapImageView(image, EImageAspect::Color);
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create cubemap image view: {}", _label);
        image = nullptr;
        return;
    }

    std::vector<uint8_t> stagingData(static_cast<size_t>(totalSize));
    for (size_t i = 0; i < CubeFace_Count; ++i) {
        std::memcpy(stagingData.data() + (i * faceSize), pixels[i].get(), static_cast<size_t>(faceSize));
    }

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        BufferCreateInfo{
            .label         = std::format("StagingBuffer_CubeMap_{}", _label),
            .usage         = EBufferUsage::TransferSrc,
            .data          = stagingData.data(),
            .size          = static_cast<uint32_t>(totalSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    auto *cmdBuf = render->beginIsolateCommands();

    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = _mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::Undefined, EImageLayout::TransferDst, &cubeRange);

    BufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
             .aspectMask     = EImageAspect::Color,
             .mipLevel       = 0,
             .baseArrayLayer = 0,
             .layerCount     = CubeFace_Count,
        },
        .imageOffsetX      = 0,
        .imageOffsetY      = 0,
        .imageOffsetZ      = 0,
        .imageExtentWidth  = _width,
        .imageExtentHeight = _height,
        .imageExtentDepth  = 1,
    };

    cmdBuf->copyBufferToImage(stagingBuffer.get(), image.get(), EImageLayout::TransferDst, {region});

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal, &cubeRange);

    render->endIsolateCommands(cmdBuf);

    YA_CORE_INFO("Created cubemap: {} ({}x{}x{})", _label, _width, _height, (int)CubeFace_Count);
}

} // namespace ya
