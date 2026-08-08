#include "Foundation/RHI/Core/Texture.h"
#include <vulkan/vulkan.h>

#include "Foundation/RHI/Core/BuiltinTextureSource.h"
#include "stb/stb_image.h"

#include <bit>
#include <cstddef>
#include <cstring>

#include "Foundation/RHI/Core/Buffer.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Core/Image.h"
#include "Foundation/RHI/Core/RenderResourceFactory.h"
#include "Foundation/RHI/Core/TextureUploadService.h"
#include "Foundation/RHI/Render.h"

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
    uint32_t             width    = 0;
    uint32_t             height   = 0;
    uint32_t             channels = 4;
    EFormat::T           format   = EFormat::R8G8B8A8_UNORM;
    std::vector<uint8_t> bytes;

    [[nodiscard]] bool isValid() const
    {
        return !bytes.empty() && width > 0 && height > 0;
    }
};

std::vector<ColorRGBA<uint8_t>> buildMissingTexturePixels()
{
    constexpr uint32_t size = 8;
    const ColorRGBA<uint8_t> purple{.r = 255, .g = 0, .b = 255, .a = 255};
    const ColorRGBA<uint8_t> black{.r = 0, .g = 0, .b = 0, .a = 255};

    std::vector<ColorRGBA<uint8_t>> pixels(size * size);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            pixels[y * size + x] = ((x + y) % 2 == 0) ? purple : black;
        }
    }
    return pixels;
}

bool copyFaceToStaging(uint8_t*                 dst,
                       const TextureMemoryView& face,
                       size_t                   faceSize,
                       bool                     flipVertical)
{
    if (!dst || !face.isValid() || face.dataSize < faceSize) {
        return false;
    }

    const auto* src = static_cast<const uint8_t*>(face.data);
    if (!flipVertical) {
        std::memcpy(dst, src, faceSize);
        return true;
    }

    if (face.height == 0 || faceSize % face.height != 0) {
        YA_CORE_ERROR("Texture cubemap face row stride is invalid");
        return false;
    }

    const size_t rowSize = faceSize / face.height;
    for (uint32_t row = 0; row < face.height; ++row) {
        const uint32_t srcRow = face.height - 1 - row;
        std::memcpy(dst + row * rowSize, src + srcRow * rowSize, rowSize);
    }

    return true;
}

IRenderResourceFactory& getResourceFactory(IRender& render)
{
    auto* resourceFactory = render.getResourceFactory();
    YA_CORE_ASSERT(resourceFactory, "Render resource factory is not available");
    return *resourceFactory;
}
} // namespace

std::shared_ptr<Texture> Texture::fromMemory(IRender& render, const TextureMemoryCreateInfo& ci)
{
    if (!ci.memory.isValid()) {
        YA_CORE_ERROR("Texture::fromMemory: invalid texture memory for '{}'", ci.filepath);
        return nullptr;
    }

    auto texture       = Texture::createShared();
    texture->_filepath = ci.filepath;
    texture->_label    = ci.label.empty() ? ci.filepath : ci.label;
    texture->_channels = ci.memory.channels;
    texture->initFromData(render,
                          ci.memory.data,
                          ci.memory.dataSize,
                          ci.memory.width,
                          ci.memory.height,
                          ci.memory.format,
                          ci.memory.mipLevels,
                          ci.memory.generateMipmaps);

    YA_CORE_TRACE("Created texture from memory: {} ({}x{})", texture->_label, ci.memory.width, ci.memory.height);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(IRender&                               render,
                                           uint32_t                               width,
                                           uint32_t                               height,
                                           const std::vector<ColorRGBA<uint8_t>>& data,
                                           const std::string&                     label)
{
    YA_CORE_ASSERT(static_cast<uint32_t>(data.size()) == width * height,
                   "Pixel data size does not match width * height");

    auto texture       = Texture::createShared();
    texture->_label    = label;
    texture->_channels = 4;
    texture->initFromData(render, data.data(), 0, width, height, EFormat::R8G8B8A8_UNORM);

    YA_CORE_TRACE("Created texture from RGBA data ({}x{}) label: {}", width, height, label);
    return texture;
}

std::shared_ptr<Texture> Texture::fromData(IRender&           render,
                                           uint32_t           width,
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

    texture->initFromData(render, data, dataSize, width, height, format, 1);

    YA_CORE_TRACE("Created texture from raw data ({}x{}, format: {}) label: {}",
                  width,
                  height,
                  static_cast<int>(format),
                  label);
    return texture;
}

std::shared_ptr<Texture> Texture::createCubeMap(IRender& render, const CubeMapCreateInfo& ci)
{
    std::array<OwnedCubeMapFace, CubeFace_Count> ownedFaces{};
    CubeMapMemoryCreateInfo                      memoryCI;
    memoryCI.label        = ci.label;
    memoryCI.flipVertical = ci.flipVertical;

    int width    = -1;
    int height   = -1;
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

    return createCubeMapFromMemory(render, memoryCI);
}

std::shared_ptr<Texture> Texture::createCubeMapFromMemory(IRender& render, const CubeMapMemoryCreateInfo& ci)
{
    if (!ci.isValid()) {
        YA_CORE_ERROR("Texture::createCubeMapFromMemory: invalid input");
        return nullptr;
    }

    auto texture    = Texture::createShared();
    texture->_label = ci.label;
    texture->initCubeMapFromMemory(render, ci);

    if (!texture->isValid()) {
        return nullptr;
    }

    return texture;
}

std::shared_ptr<Texture> Texture::createSolidCubeMap(IRender& render, const ColorU8_t& color, const std::string& label)
{
    auto texture        = Texture::createShared();
    texture->_label     = label.empty() ? "SolidCubeMap" : label;
    texture->_width     = 1;
    texture->_height    = 1;
    texture->_channels  = 4;
    texture->_format    = EFormat::R8G8B8A8_UNORM;
    texture->_mipLevels = 1;

    auto& resourceFactory = getResourceFactory(render);

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

    texture->image = resourceFactory.createImage(imageCI);
    if (!texture->image || !texture->image->getHandle()) {
        YA_CORE_ERROR("Failed to create solid cubemap image: {}", texture->_label);
        return nullptr;
    }

    texture->imageView = resourceFactory.createImageView(
        texture->image,
        ImageViewCreateInfo{
            .label       = std::format("CubeMap_ImageView_{}", texture->_label),
            .viewType    = EImageViewType::ViewCube,
            .aspectFlags = EImageAspect::Color,
            .layerCount  = CubeFace_Count,
        });
    if (!texture->imageView || !texture->imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create solid cubemap image view: {}", texture->_label);
        texture->image.reset();
        return nullptr;
    }

    std::shared_ptr<IBuffer> stagingBuffer = resourceFactory.createBuffer(
        BufferCreateInfo{
            .label       = std::format("StagingBuffer_CubeMap_{}", texture->_label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = facePixels.data(),
            .size        = static_cast<uint32_t>(sizeof(facePixels)),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = texture->_mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };

    TextureUploadService uploadService;
    if (!uploadService.upload(
            render,
            TextureUploadRequest{
                .image   = texture->image,
                .staging = stagingBuffer,
                .regions = {{
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
                }},
                .uploadRange   = cubeRange,
                .finalizeRange = cubeRange,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                .label         = std::format("CubeMapUpload:{}:1x1", texture->_label),
            })) {
        YA_CORE_ERROR("Failed to upload solid cubemap: {}", texture->_label);
        texture->image.reset();
        texture->imageView.reset();
        return nullptr;
    }

    return texture->isValid() ? texture : nullptr;
}

std::shared_ptr<Texture> Texture::wrap(std::shared_ptr<IImage>     img,
                                       std::shared_ptr<IImageView> view,
                                       const std::string&          label)
{
    YA_CORE_ASSERT(img && view, "Cannot wrap null IImage or IImageView");

    auto texture = createShared();

    texture->_label     = label;
    texture->image      = img;
    texture->imageView  = view;
    texture->_width     = img->getWidth();
    texture->_height    = img->getHeight();
    texture->_format    = img->getFormat();
    texture->_mipLevels = 1;
    texture->_channels  = 4;

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

void Texture::initFromData(IRender& render,
                           const void* pixels,
                           size_t      dataSize,
                           uint32_t    texWidth,
                           uint32_t    texHeight,
                           EFormat::T  format,
                           uint32_t    mipLevels,
                           bool        generateMipmaps)
{
    auto& resourceFactory = getResourceFactory(render);

    const bool bGenerateMipmaps = generateMipmaps && mipLevels == 1 && render.supportsMipGeneration(format);
    if (generateMipmaps && !bGenerateMipmaps) {
        YA_CORE_WARN("GPU mip generation is not supported for texture '{}' format {}; uploading base level only",
                     _filepath.empty() ? _label : _filepath,
                     static_cast<int>(format));
    }

    _width     = texWidth;
    _height    = texHeight;
    _format    = format;
    _mipLevels = bGenerateMipmaps ? std::bit_width(std::max(texWidth, texHeight)) : mipLevels;

    VkDeviceSize imageSize = 0;
    if (dataSize > 0) {
        imageSize = dataSize;
    }
    else {
        const size_t pixelSize = EFormat::getPixelSize(format);
        imageSize = pixelSize * texWidth * texHeight;
    }

    auto ci = ya::ImageCreateInfo{
        .label  = std::format("Texture_Image_{}", _label),
        .format = format,
        .extent = {
            .width  = texWidth,
            .height = texHeight,
            .depth  = 1,
        },
        .mipLevels     = _mipLevels,
        .samples       = ESampleCount::Sample_1,
        .usage         = static_cast<EImageUsage::T>(EImageUsage::Sampled |
                                                     EImageUsage::TransferDst |
                                                     (bGenerateMipmaps ? EImageUsage::TransferSrc : 0)),
        .initialLayout = EImageLayout::Undefined,
    };

    image = resourceFactory.createImage(ci);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create image for texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      static_cast<int>(format),
                      texWidth,
                      texHeight);
        const auto fallbackPixels = buildMissingTexturePixels();
        initFallbackTexture(render,
                            fallbackPixels.data(),
                            fallbackPixels.size() * sizeof(ColorRGBA<uint8_t>),
                            8,
                            8);
        return;
    }

    std::shared_ptr<IBuffer> stagingBuffer = resourceFactory.createBuffer(
        ya::BufferCreateInfo{
            .label       = std::format("StagingBuffer_Texture_{}", _filepath.empty() ? _label : _filepath),
            .usage       = EBufferUsage::TransferSrc,
            .data        = const_cast<void*>(pixels),
            .size        = static_cast<uint32_t>(imageSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    std::vector<BufferImageCopy> regions;
    const bool                   isCompressed = EFormat::isBlockCompressed(format);

    if (mipLevels > 1 && dataSize > 0) {
        VkDeviceSize bufferOffset  = 0;
        uint32_t     currentWidth  = texWidth;
        uint32_t     currentHeight = texHeight;
        const auto   blockExtent   = EFormat::getCompressedBlockExtent(format);

        for (uint32_t level = 0; level < mipLevels; level++) {
            currentWidth  = std::max(currentWidth, 1u);
            currentHeight = std::max(currentHeight, 1u);

            const uint32_t pixelOrBlockSize = static_cast<uint32_t>(EFormat::getPixelSize(format));
            const uint32_t blocksX = isCompressed ? (currentWidth + blockExtent.width - 1) / blockExtent.width : currentWidth;
            const uint32_t blocksY = isCompressed ? (currentHeight + blockExtent.height - 1) / blockExtent.height : currentHeight;
            const VkDeviceSize levelSize = static_cast<VkDeviceSize>(blocksX) *
                                           static_cast<VkDeviceSize>(blocksY) *
                                           static_cast<VkDeviceSize>(pixelOrBlockSize);

            if (bufferOffset + levelSize > imageSize) {
                YA_CORE_ERROR("Mip level {} data exceeds buffer size: {} > {}",
                              level,
                              bufferOffset + levelSize,
                              imageSize);
                break;
            }

            regions.push_back(BufferImageCopy{
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
            });

            bufferOffset += levelSize;
            currentWidth /= 2;
            currentHeight /= 2;
        }
    }
    else {
        regions.push_back(BufferImageCopy{
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
        });
    }

    TextureUploadService uploadService;
    uint32_t             uploadedMipLevels = _mipLevels;
    if (!uploadService.upload(
            render,
            TextureUploadRequest{
                .image            = image,
                .staging          = stagingBuffer,
                .regions          = std::move(regions),
                .bGenerateMipmaps = bGenerateMipmaps,
                .finalLayout      = EImageLayout::ShaderReadOnlyOptimal,
                .label            = std::format(
                    "{}:{}x{}:mips{}",
                    _filepath.empty() ? _label : _filepath,
                    texWidth,
                    texHeight,
                    _mipLevels),
            },
            &uploadedMipLevels)) {
        YA_CORE_ERROR("Failed to upload texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      static_cast<int>(format),
                      texWidth,
                      texHeight);
        image.reset();
        const auto fallbackPixels = buildMissingTexturePixels();
        initFallbackTexture(render,
                            fallbackPixels.data(),
                            fallbackPixels.size() * sizeof(ColorRGBA<uint8_t>),
                            8,
                            8);
        return;
    }
    _mipLevels = uploadedMipLevels;

    ImageViewCreateInfo viewCI{
        .label       = std::format("Texture_ImageView_{}", _label),
        .aspectFlags = EImageAspect::Color,
        .levelCount  = _mipLevels,
    };
    imageView = resourceFactory.createImageView(image, viewCI);
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create image view for texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      static_cast<int>(format),
                      texWidth,
                      texHeight);
        image.reset();
        const auto fallbackPixels = buildMissingTexturePixels();
        initFallbackTexture(render,
                            fallbackPixels.data(),
                            fallbackPixels.size() * sizeof(ColorRGBA<uint8_t>),
                            8,
                            8);
    }
}

void Texture::initFallbackTexture(IRender& render, const void* pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = EFormat::R8G8B8A8_UNORM;
    _mipLevels = 1;
    _channels  = 4;

    auto& resourceFactory = getResourceFactory(render);

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

    image = resourceFactory.createImage(ci);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create fallback texture!");
        return;
    }

    ImageViewCreateInfo fallbackViewCI{
        .label       = std::format("Texture_Fallback_ImageView_{}", _label),
        .aspectFlags = EImageAspect::Color,
    };
    imageView = resourceFactory.createImageView(image, fallbackViewCI);
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create fallback texture image view!");
        image = nullptr;
        return;
    }

    std::shared_ptr<IBuffer> stagingBuffer = resourceFactory.createBuffer(
        ya::BufferCreateInfo{
            .label       = std::format("StagingBuffer_Fallback_{}", _label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = const_cast<void*>(pixels),
            .size        = static_cast<uint32_t>(dataSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    TextureUploadService uploadService;
    if (!uploadService.upload(
            render,
            TextureUploadRequest{
                .image   = image,
                .staging = stagingBuffer,
                .regions = {{
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
                }},
                .finalLayout = EImageLayout::ShaderReadOnlyOptimal,
                .label       = std::format(
                    "FallbackTexture:{}:{}x{}",
                    _filepath.empty() ? _label : _filepath,
                    texWidth,
                    texHeight),
            })) {
        YA_CORE_ERROR("Failed to upload fallback texture for '{}'!", _filepath.empty() ? _label : _filepath);
        image = nullptr;
        imageView = nullptr;
        return;
    }

    YA_CORE_WARN("Created fallback texture ({}x{}) for: {}", texWidth, texHeight, _filepath.empty() ? _label : _filepath);
}

void Texture::initCubeMapFromMemory(IRender& render, const CubeMapMemoryCreateInfo& ci)
{
    auto& resourceFactory = getResourceFactory(render);

    _width     = ci.faces[0].width;
    _height    = ci.faces[0].height;
    _channels  = ci.faces[0].channels;
    _format    = ci.faces[0].format;
    _mipLevels = 1;

    const VkDeviceSize faceSize  = static_cast<VkDeviceSize>(ci.faces[0].baseLevelDataSize());
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

    image = resourceFactory.createImage(imageCI);
    if (!image || !image->getHandle()) {
        YA_CORE_ERROR("Failed to create cubemap image: {}", _label);
        return;
    }

    imageView = resourceFactory.createImageView(
        image,
        ImageViewCreateInfo{
            .label       = std::format("CubeMap_ImageView_{}", _label),
            .viewType    = EImageViewType::ViewCube,
            .aspectFlags = EImageAspect::Color,
            .layerCount  = CubeFace_Count,
        });
    if (!imageView || !imageView->getHandle()) {
        YA_CORE_ERROR("Failed to create cubemap image view: {}", _label);
        image = nullptr;
        return;
    }

    std::vector<uint8_t> stagingData(static_cast<size_t>(totalSize));
    for (size_t i = 0; i < CubeFace_Count; ++i) {
        if (!copyFaceToStaging(stagingData.data() + (i * faceSize),
                               ci.faces[i],
                               static_cast<size_t>(faceSize),
                               ci.flipVertical)) {
            image.reset();
            imageView.reset();
            return;
        }
    }

    std::shared_ptr<IBuffer> stagingBuffer = resourceFactory.createBuffer(
        BufferCreateInfo{
            .label       = std::format("StagingBuffer_CubeMap_{}", _label),
            .usage       = EBufferUsage::TransferSrc,
            .data        = stagingData.data(),
            .size        = static_cast<uint32_t>(totalSize),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });

    ImageSubresourceRange cubeRange{
        .aspectMask     = EImageAspect::Color,
        .baseMipLevel   = 0,
        .levelCount     = _mipLevels,
        .baseArrayLayer = 0,
        .layerCount     = CubeFace_Count,
    };

    TextureUploadService uploadService;
    if (!uploadService.upload(
            render,
            TextureUploadRequest{
                .image   = image,
                .staging = stagingBuffer,
                .regions = {{
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
                }},
                .uploadRange   = cubeRange,
                .finalizeRange = cubeRange,
                .finalLayout   = EImageLayout::ShaderReadOnlyOptimal,
                .label         = std::format("CubeMapUpload:{}:{}x{}", _label, _width, _height),
            })) {
        YA_CORE_ERROR("Failed to upload cubemap: {}", _label);
        image.reset();
        imageView.reset();
        return;
    }

    YA_CORE_INFO("Created cubemap: {} ({}x{}x{})", _label, _width, _height, static_cast<int>(CubeFace_Count));
}

ImageViewHandle TextureBinding::getImageViewHandle() const
{
    if (texture && texture->getImageView()) {
        return texture->getImageView()->getHandle();
    }
    if (auto* source = getBuiltinTextureSource()) {
        if (auto white = source->getWhiteTexture(); white && white->getImageView()) {
            return white->getImageView()->getHandle();
        }
    }
    return ImageViewHandle{};
}

SamplerHandle TextureBinding::getSamplerHandle() const
{
    if (sampler) {
        return sampler->getHandle();
    }
    if (auto* source = getBuiltinTextureSource()) {
        if (auto defaultSampler = source->getDefaultSampler()) {
            return defaultSampler->getHandle();
        }
    }
    return SamplerHandle{};
}

} // namespace ya
