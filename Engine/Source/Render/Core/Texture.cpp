#include "Texture.h"

#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "stb/stb_image.h"

#include <cstddef>
#include <cstring>

#include "Render/Core/CommandBuffer.h"

#include "Render/Core/Buffer.h"
#include "Render/Core/Image.h"
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
    explicit StbiImage(stbi_uc* ptr)
    {
        data = std::shared_ptr<stbi_uc>(ptr, [](stbi_uc* p) {
            stbi_image_free(p);
        });
    }

    operator bool() const { return data != nullptr; }
    const stbi_uc* get() const { return data.get(); }
    stbi_uc*       get() { return data.get(); }
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

struct OwnedCubeMapFace
{
    uint32_t            width    = 0;
    uint32_t            height   = 0;
    uint32_t            channels = 4;
    EFormat::T          format   = EFormat::R8G8B8A8_UNORM;
    std::vector<uint8_t> bytes;

    [[nodiscard]] bool isValid() const
    {
        return !bytes.empty() && width > 0 && height > 0;
    }
};

static bool copyFaceToStaging(uint8_t* dst, const TextureMemoryView& face, bool flipVertical)
{
    if (!dst || !face.isValid()) {
        return false;
    }

    const auto* src = static_cast<const uint8_t*>(face.data);
    if (!flipVertical) {
        std::memcpy(dst, src, face.dataSize);
        return true;
    }

    if (face.height == 0 || face.dataSize % face.height != 0) {
        YA_CORE_ERROR("Texture cubemap face row stride is invalid");
        return false;
    }

    const size_t rowSize = face.dataSize / face.height;
    for (uint32_t row = 0; row < face.height; ++row) {
        const uint32_t srcRow = face.height - 1 - row;
        std::memcpy(dst + row * rowSize, src + srcRow * rowSize, rowSize);
    }

    return true;
}
} // namespace

static size_t getFormatPixelSize(EFormat::T format);
static bool   isBlockCompressed(EFormat::T format);

static size_t getFormatPixelSize(EFormat::T format)
{
    switch (format) {
    case EFormat::R8_UNORM:
        return 1;
    case EFormat::R8G8_UNORM:
        return 2;
    case EFormat::R32_SFLOAT:
        return 4;
    case EFormat::R16G16B16A16_SFLOAT:
        return 8;
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::R8G8B8A8_SRGB:
    case EFormat::B8G8R8A8_UNORM:
    case EFormat::B8G8R8A8_SNORM:
    case EFormat::B8G8R8A8_SRGB:
    case EFormat::D32_SFLOAT:
    case EFormat::D24_UNORM_S8_UINT:
        return 4;
    case EFormat::D32_SFLOAT_S8_UINT:
        return 5;
    case EFormat::BC1_RGB_UNORM_BLOCK:
    case EFormat::BC1_RGBA_UNORM_BLOCK:
    case EFormat::BC1_RGB_SRGB_BLOCK:
    case EFormat::BC1_RGBA_SRGB_BLOCK:
    case EFormat::BC4_UNORM_BLOCK:
    case EFormat::BC4_SNORM_BLOCK:
        return 8;
    case EFormat::BC3_UNORM_BLOCK:
    case EFormat::BC3_SRGB_BLOCK:
    case EFormat::BC5_UNORM_BLOCK:
    case EFormat::BC5_SNORM_BLOCK:
    case EFormat::BC7_UNORM_BLOCK:
    case EFormat::BC7_SRGB_BLOCK:
        return 16;
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
        return 4;
    }
}

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

ITextureFactory* Texture::getTextureFactory()
{
    auto render = ya::App::get()->getRender();
    YA_CORE_ASSERT(render, "Render is not initialized");

    auto textureFactory = render->getTextureFactory();
    YA_CORE_ASSERT(textureFactory && textureFactory->isValid(), "TextureFactory is not available");

    return textureFactory;
}

std::shared_ptr<Texture> Texture::fromMemory(const TextureMemoryCreateInfo& ci)
{
    if (!ci.memory.isValid()) {
        YA_CORE_ERROR("Texture::fromMemory: invalid texture memory for '{}'", ci.filepath);
        return nullptr;
    }

    auto texture       = Texture::createShared();
    texture->_filepath = ci.filepath;
    texture->_label    = ci.label.empty() ? ci.filepath : ci.label;
    texture->_channels = ci.memory.channels;
    texture->initFromData(ci.memory.data,
                          ci.memory.dataSize,
                          ci.memory.width,
                          ci.memory.height,
                          ci.memory.format);

    YA_CORE_TRACE("Created texture from memory: {} ({}x{})", texture->_label, ci.memory.width, ci.memory.height);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(uint32_t                               width,
                                           uint32_t                               height,
                                           const std::vector<ColorRGBA<uint8_t>>& data,
                                           const std::string&                     label)
{
    YA_CORE_ASSERT(static_cast<uint32_t>(data.size()) == width * height,
                   "Pixel data size does not match width * height");

    auto texture       = Texture::createShared();
    texture->_label    = label;
    texture->_channels = 4;
    texture->initFromData(data.data(), 0, width, height, EFormat::R8G8B8A8_UNORM);

    YA_CORE_TRACE("Created texture from RGBA data ({}x{}) label: {}", width, height, label);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(uint32_t           width,
                                           uint32_t           height,
                                           const void*        data,
                                           size_t             dataSize,
                                           EFormat::T         format,
                                           const std::string& label)
{
    auto texture     = Texture::createShared();
    texture->_label  = label;
    texture->_format = format;

    switch (format) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::B8G8R8A8_UNORM:
        texture->_channels = 4;
        break;
    default:
        texture->_channels = 4;
        break;
    }

    texture->initFromData(data, dataSize, width, height, format, 1);

    YA_CORE_TRACE("Created texture from raw data ({}x{}, format: {}) label: {}", width, height, static_cast<int>(format), label);
    return texture;
}

std::shared_ptr<Texture> Texture::createCubeMap(const CubeMapCreateInfo& ci)
{
    std::array<OwnedCubeMapFace, CubeFace_Count> ownedFaces{};
    CubeMapMemoryCreateInfo memoryCI;
    memoryCI.label        = ci.label;
    memoryCI.flipVertical = ci.flipVertical;

    int width = -1;
    int height = -1;
    int channels = -1;

    for (size_t i = 0; i < CubeFace_Count; ++i) {
        stbi_uc* raw = stbi_load(ci.files[i].c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!raw) {
            YA_CORE_ERROR("Failed to load cubemap face {}: {}", i, ci.files[i]);
            return nullptr;
        }

        auto& face    = ownedFaces[i];
        face.width    = static_cast<uint32_t>(width);
        face.height   = static_cast<uint32_t>(height);
        face.channels = 4;
        face.format   = EFormat::R8G8B8A8_UNORM;
        face.bytes.resize(static_cast<size_t>(face.width) * face.height * face.channels);
        std::memcpy(face.bytes.data(), raw, face.bytes.size());
        stbi_image_free(raw);

        memoryCI.faces[i] = TextureMemoryView{
            .width    = face.width,
            .height   = face.height,
            .channels = face.channels,
            .format   = face.format,
            .data     = face.bytes.data(),
            .dataSize = face.bytes.size(),
        };
    }

    return createCubeMapFromMemory(memoryCI);
}

std::shared_ptr<Texture> Texture::createCubeMapFromMemory(const CubeMapMemoryCreateInfo& ci)
{
    if (!ci.isValid()) {
        YA_CORE_ERROR("Texture::createCubeMapFromMemory: invalid input");
        return nullptr;
    }

    auto texture    = Texture::createShared();
    texture->_label = ci.label;
    texture->initCubeMapFromMemory(ci);

    if (!texture->isValid()) {
        return nullptr;
    }

    return texture;
}

std::shared_ptr<Texture> Texture::createSolidCubeMap(const ColorU8_t& color, const std::string& label)
{
    auto texture        = Texture::createShared();
    texture->_label     = label.empty() ? "SolidCubeMap" : label;
    texture->_width     = 1;
    texture->_height    = 1;
    texture->_channels  = 4;
    texture->_format    = EFormat::R8G8B8A8_UNORM;
    texture->_mipLevels = 1;

    auto textureFactory = getTextureFactory();
    auto render         = textureFactory->getRender();

    std::array<ColorU8_t, CubeFace_Count> facePixels{};
    facePixels.fill(color);

    ImageCreateInfo imageCI{
        .label  = std::format("CubeMap_{}", texture->_label),
        .format = texture->_format,
        .extent = {
            .width  = 1,
            .height = 1,
            .depth  = 1,
        },
        .mipLevels     = 1,
        .arrayLayers   = CubeFace_Count,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled | EImageUsage::TransferDst),
        .initialLayout = EImageLayout::Undefined,
        .flags         = EImageCreateFlag::CubeCompatible,
    };

    texture->image = textureFactory->createImage(imageCI);
    if (!texture->image || !texture->image->getHandle()) {
        YA_CORE_ERROR("Failed to create solid cubemap image: {}", texture->_label);
        return nullptr;
    }

    texture->imageView = textureFactory->createCubeMapImageView(texture->image, EImageAspect::Color);
    if (!texture->imageView || !texture->imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create solid cubemap image view: {}", texture->_label);
        texture->image.reset();
        return nullptr;
    }

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        BufferCreateInfo{
            .label       = std::format("StagingBuffer_CubeMap_{}", texture->_label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = facePixels.data(),
            .size        = static_cast<uint32_t>(sizeof(facePixels)),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    auto* cmdBuf = render->beginIsolateCommands(std::format("CubeMapUpload:{}:1x1", texture->_label));

    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = texture->_mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };

    cmdBuf->transitionImageLayout(texture->image.get(), EImageLayout::Undefined, EImageLayout::TransferDst, &cubeRange);

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
        .imageExtentWidth  = 1,
        .imageExtentHeight = 1,
        .imageExtentDepth  = 1,
    };

    cmdBuf->copyBufferToImage(stagingBuffer.get(), texture->image.get(), EImageLayout::TransferDst, {region});
    cmdBuf->transitionImageLayout(texture->image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal, &cubeRange);

    render->endIsolateCommands(cmdBuf);

    return texture->isValid() ? texture : nullptr;
}

std::shared_ptr<Texture> Texture::createRenderTexture(const RenderTextureCreateInfo& ci)
{
    auto textureFactory = getTextureFactory();

    ImageCreateInfo imageCI{
        .label  = ci.label,
        .format = ci.format,
        .extent = {
            .width  = ci.width,
            .height = ci.height,
            .depth  = 1,
        },
        .mipLevels     = ci.mipLevels,
        .arrayLayers   = ci.layerCount,
        .samples       = ci.samples,
        .usage         = ci.usage,
        .initialLayout = EImageLayout::Undefined,
    };

    auto image = textureFactory->createImage(imageCI);
    if (!image) {
        YA_CORE_ERROR("Failed to create render target image: {}", ci.label);
        return nullptr;
    }

    ImageViewCreateInfo viewCI{
        .label       = std::format("RenderTexture_ImageView_{}", ci.label),
        .aspectFlags = static_cast<EImageAspect::T>(ci.isDepth ? EImageAspect::Depth : EImageAspect::Color),
        .levelCount  = ci.mipLevels,
        .layerCount  = ci.layerCount,
    };
    auto imageView = textureFactory->createImageView(image, viewCI);
    if (!imageView) {
        YA_CORE_ERROR("Failed to create render target image view: {}", ci.label);
        return nullptr;
    }

    return Texture::wrap(image, imageView, ci.label);
}

std::shared_ptr<Texture> Texture::wrap(std::shared_ptr<IImage>     img,
                                       std::shared_ptr<IImageView> view,
                                       const std::string&          label)
{
    YA_CORE_ASSERT(img && view, "Cannot wrap null IImage or IImageView");

    auto texture = createShared();

    texture->_label    = label;
    texture->image     = img;
    texture->imageView = view;
    texture->_width    = img->getWidth();
    texture->_height   = img->getHeight();
    texture->_format   = img->getFormat();
    texture->_mipLevels = 1;
    texture->_channels = 4;

    YA_CORE_TRACE("Created Texture from existing IImage/IImageView: {} ({}x{})", label, texture->_width, texture->_height);
    return texture;
}

void Texture::setLabel(const std::string& label)
{
    _label = label;
    if (image) {
        image->setDebugName(std::format("Texture_Image_{}", label));
    }
    if (imageView) {
        imageView->setDebugName(std::format("Texture_ImageView_{}", label));
    }
}

void Texture::initFromData(const void* pixels,
                           size_t      dataSize,
                           uint32_t    texWidth,
                           uint32_t    texHeight,
                           EFormat::T  format,
                           uint32_t    mipLevels)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = format;
    _mipLevels = mipLevels;

    auto render         = App::get()->getRender();
    auto textureFactory = render->getTextureFactory();

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

    image = textureFactory->createImage(ci);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create image for texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      static_cast<int>(format),
                      texWidth,
                      texHeight);
        throw std::runtime_error("Failed to create image for texture: " + _label);
    }

    ImageViewCreateInfo viewCI{
        .label       = std::format("Texture_ImageView_{}", _label),
        .aspectFlags = EImageAspect::Color,
        .levelCount  = mipLevels,
    };
    imageView = textureFactory->createImageView(image, viewCI);
    YA_CORE_ASSERT(imageView,
                   "Failed to create image view for texture: {} (format: {}, {}x{})",
                   _filepath.empty() ? _label : _filepath,
                   static_cast<int>(format),
                   texWidth,
                   texHeight);

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label       = std::format("StagingBuffer_Texture_{}", _filepath.empty() ? _label : _filepath),
            .usage       = EBufferUsage::TransferSrc,
            .data        = const_cast<void*>(pixels),
            .size        = static_cast<uint32_t>(imageSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    auto* cmdBuf = render->beginIsolateCommands(std::format(
        "TextureUpload:{}:{}x{}:mips{}",
        _filepath.empty() ? _label : _filepath,
        texWidth,
        texHeight,
        mipLevels));

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::Undefined, EImageLayout::TransferDst);

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

    cmdBuf->transitionImageLayout(image.get(), EImageLayout::TransferDst, EImageLayout::ShaderReadOnlyOptimal);

    render->endIsolateCommands(cmdBuf);
}

void Texture::initFallbackTexture(const void* pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight)
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

    ImageViewCreateInfo fallbackViewCI{
        .label       = std::format("Texture_Fallback_ImageView_{}", _label),
        .aspectFlags = EImageAspect::Color,
    };
    imageView = textureFactory->createImageView(image, fallbackViewCI);
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create fallback texture image view!");
        image = nullptr;
        return;
    }

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        ya::BufferCreateInfo{
            .label       = std::format("StagingBuffer_Fallback_{}", _label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = const_cast<void*>(pixels),
            .size        = static_cast<uint32_t>(dataSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    auto* cmdBuf = render->beginIsolateCommands(std::format(
        "FallbackTextureUpload:{}:{}x{}",
        _filepath.empty() ? _label : _filepath,
        texWidth,
        texHeight));

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

void Texture::initCubeMap(const CubeMapCreateInfo& ci)
{
    auto cubemap = createCubeMap(ci);
    if (!cubemap) {
        return;
    }

    _width     = cubemap->_width;
    _height    = cubemap->_height;
    _channels  = cubemap->_channels;
    _format    = cubemap->_format;
    _mipLevels = cubemap->_mipLevels;
    image      = cubemap->image;
    imageView  = cubemap->imageView;
}

void Texture::initCubeMapFromMemory(const CubeMapMemoryCreateInfo& ci)
{
    auto textureFactory = getTextureFactory();
    auto render         = textureFactory->getRender();

    _width     = ci.faces[0].width;
    _height    = ci.faces[0].height;
    _channels  = ci.faces[0].channels;
    _format    = ci.faces[0].format;
    _mipLevels = 1;

    const VkDeviceSize faceSize  = static_cast<VkDeviceSize>(ci.faces[0].dataSize);
    const VkDeviceSize totalSize = faceSize * CubeFace_Count;

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
        if (!copyFaceToStaging(stagingData.data() + (i * faceSize), ci.faces[i], ci.flipVertical)) {
            image.reset();
            imageView.reset();
            return;
        }
    }

    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        render,
        BufferCreateInfo{
            .label       = std::format("StagingBuffer_CubeMap_{}", _label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = stagingData.data(),
            .size        = static_cast<uint32_t>(totalSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    auto* cmdBuf = render->beginIsolateCommands(std::format(
        "CubeMapUpload:{}:{}x{}",
        _label,
        _width,
        _height));

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

    YA_CORE_INFO("Created cubemap: {} ({}x{}x{})", _label, _width, _height, static_cast<int>(CubeFace_Count));
}

ImageViewHandle TextureBinding::getImageViewHandle() const
{
    if (texture && texture->getImageView()) {
        return texture->getImageView()->getHandle();
    }
    return TextureLibrary::get().getWhiteTexture()->getImageView()->getHandle();
}

SamplerHandle TextureBinding::getSamplerHandle() const
{
    if (sampler) {
        return sampler->getHandle();
    }
    return TextureLibrary::get().getDefaultSampler()->getHandle();
}

} // namespace ya
