#include "Texture.h"

#include "Core/App/App.h"
#include "stb/stb_image.h"

#include <cstddef>

#include "Platform/Render/Vulkan/VulkanUtils.h"

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

#include "Render/Core/Buffer.h"
#include "Render/Render.h"

#include "ktx.h"


namespace ya
{



// Helper function to calculate pixel size or block size
static size_t getFormatPixelSize(EFormat::T format)
{
    switch (format) {
    // Uncompressed formats
    case EFormat::R8_UNORM:
        return 1;
    case EFormat::R8G8_UNORM:
        return 2;
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::B8G8R8A8_UNORM:
        return 4;

    // Block compressed formats (BC1-DXT1: 4x4 blocks, 8 bytes per block)
    case EFormat::BC1_RGB_UNORM_BLOCK:
    case EFormat::BC1_RGBA_UNORM_BLOCK:
    case EFormat::BC1_RGB_SRGB_BLOCK:
    case EFormat::BC1_RGBA_SRGB_BLOCK:
        return 8;

    // Block compressed formats (BC3-DXT5: 4x4 blocks, 16 bytes per block)
    case EFormat::BC3_UNORM_BLOCK:
    case EFormat::BC3_SRGB_BLOCK:
        return 16;

    // Block compressed formats (BC7: 4x4 blocks, 16 bytes per block)
    case EFormat::BC7_UNORM_BLOCK:
    case EFormat::BC7_SRGB_BLOCK:
        return 16;

    // Block compressed formats (BC4: 4x4 blocks, 8 bytes per block)
    case EFormat::BC4_UNORM_BLOCK:
    case EFormat::BC4_SNORM_BLOCK:
        return 8;

    // Block compressed formats (BC5: 4x4 blocks, 16 bytes per block)
    case EFormat::BC5_UNORM_BLOCK:
    case EFormat::BC5_SNORM_BLOCK:
        return 16;

    // ASTC formats (4x4 blocks, 16 bytes per block)
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

    // ETC2 formats (4x4 blocks, 8 or 16 bytes per block)
    case EFormat::ETC2_R8G8B8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8_SRGB_BLOCK:
    case EFormat::ETC2_R8G8B8A1_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A1_SRGB_BLOCK:
        return 8;
    case EFormat::ETC2_R8G8B8A8_UNORM_BLOCK:
    case EFormat::ETC2_R8G8B8A8_SRGB_BLOCK:
        return 16;

    default:
        YA_CORE_WARN("Unknown format {}, assuming 4 bytes per pixel", (int)format);
        return 4;
    }
}

// Helper function to check if format is block compressed
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

Texture::Texture(const std::string &filepath)
{

    stdpath p = filepath;
    if (p.extension() == ".ktx" || p.extension() == ".ktx2") {
        // Load KTX texture
        ktxTexture2   *tex    = nullptr;
        KTX_error_code result = ktxTexture2_CreateFromNamedFile(filepath.c_str(), KTX_TEXTURE_CREATE_NO_FLAGS, &tex);

        if (result != KTX_SUCCESS) {
            YA_CORE_ERROR("Failed to load KTX texture: {}", filepath.c_str());
            return;
        }

        // Get texture properties
        ktx_uint32_t width     = tex->baseWidth;
        ktx_uint32_t height    = tex->baseHeight;
        ktx_uint32_t mipLevels = tex->numLevels;

        VkFormat   vkFormat = static_cast<VkFormat>(tex->vkFormat);
        EFormat::T format   = EFormat::fromVk(vkFormat);

        if (format == EFormat::Undefined) {
            YA_CORE_ERROR("Unsupported KTX format: {}", tex->vkFormat);
            ktxTexture_Destroy(ktxTexture(tex));
            return;
        }

        auto vkRender = ya::App::get()->getRender<VulkanRender>();

        // Calculate total size needed for staging buffer
        ktx_size_t totalSize = ktxTexture_GetDataSize(ktxTexture(tex));

        // Upload the KTX texture data to GPU directly using KTX's upload function
        // This is the most reliable way to handle block compressed formats
        ktx_uint8_t *ktxData     = ktxTexture_GetData(ktxTexture(tex));
        ktx_size_t   ktxDataSize = ktxTexture_GetDataSize(ktxTexture(tex));

        // Note: For KTX with block compression, we need to properly handle mip level offsets
        // The createImage function will handle the per-level copying

        createImage(ktxData, ktxDataSize, width, height, format, mipLevels);

        ktxTexture_Destroy(ktxTexture(tex));

        _filepath = filepath;
        YA_CORE_TRACE("Created KTX texture from file: {} ({}x{}, {} mips, format: {}, data size: {})",
                      filepath.data(),
                      width,
                      height,
                      mipLevels,
                      (int)format,
                      ktxDataSize);
        return;
    }

    int   texWidth = -1, texHeight = -1, texChannels = -1;
    void *pixels = nullptr;
    // TODO: support other formats, eg: ktx
    pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        YA_CORE_ERROR("failed to load texture image! {}", filepath.data());
        return;
    }

    _filepath = filepath;
    _channels = 4; // Force RGBA
    createImage(pixels, 0, (uint32_t)texWidth, (uint32_t)texHeight, EFormat::R8G8B8A8_UNORM);
    stbi_image_free(pixels);
    YA_CORE_TRACE("Created texture from file: {} ({}x{}, {} channels)", filepath.data(), texWidth, texHeight, texChannels);
}

Texture::Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data)
{
    YA_CORE_ASSERT((uint32_t)data.size() == width * height, "pixel data size does not match width * height");
    _channels = 4;
    createImage(data.data(), 0, width, height, EFormat::R8G8B8A8_UNORM);
    YA_CORE_TRACE("Created texture from data({}x{})", width, height);
}

Texture::Texture(uint32_t width, uint32_t height, const void *data, size_t dataSize, EFormat::T format)
{
    _format = format;
    // Derive channels from format
    switch (format) {
    case EFormat::R8G8B8A8_UNORM:
    case EFormat::B8G8R8A8_UNORM:
        _channels = 4;
        break;
    default:
        _channels = 4; // Default
        break;
    }

    createImage(data, dataSize, width, height, format, 1);
    YA_CORE_TRACE("Created texture from raw data ({}x{}, format: {})", width, height, (int)format);
}

// Platform-independent accessor
FormatHandle Texture::getFormatHandle() const
{
    // For now, we'll store the VkFormat in the FormatHandle
    // In a more complete abstraction, you'd have a Format enum or interface
    return FormatHandle{reinterpret_cast<void *>(static_cast<std::uintptr_t>(toVk(_format)))};
}

void Texture::setLabel(const std::string &label)
{
    _label = label;
    image->setDebugName(std::format("Texture_Image_{}", label));
    imageView->setDebugName(std::format("Texture_ImageView_{}", label));
}

void Texture::createImage(const void *pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight, EFormat::T format, uint32_t mipLevels)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = format;
    _mipLevels = mipLevels;

    auto vkRender = ya::App::get()->getRender<VulkanRender>();

    // Calculate image size
    // For uncompressed formats, calculate based on pixel size
    // For block compressed formats, dataSize should be provided directly from KTX
    VkDeviceSize imageSize;
    if (dataSize > 0) {
        // Use provided data size (typically from KTX with all mip levels)
        imageSize = dataSize;
    }
    else {
        // Calculate for uncompressed single mip level
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
        .sharingMode   = ESharingMode::Exclusive,
        .initialLayout = EImageLayout::Undefined,
    };

    // Create Vulkan image
    auto vkImage = VulkanImage::create(vkRender, ci);
    // TODO: some format(eg: VK_FORMAT_ASTC_5x5_SRGB_BLOCK ) now supported for now , simply skip it
    if (!vkImage || !vkImage->isValid()) {
        YA_CORE_ERROR("Failed to create VulkanImage for texture: {} (format: {}, {}x{})",
                      _filepath.empty() ? _label : _filepath,
                      (int)format,
                      texWidth,
                      texHeight);

        // Create a fallback 1x1 magenta texture to indicate error
        uint32_t magentaPixel = 0xFFFF00FF; // RGBA: magenta
        createFallbackTexture(&magentaPixel, sizeof(magentaPixel), 1, 1);
        return;
    }


    // Store as abstract interface
    image     = vkImage;
    imageView = VulkanImageView::create(vkRender, vkImage, VK_IMAGE_ASPECT_COLOR_BIT);

    // Create staging buffer
    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        vkRender,
        ya::BufferCreateInfo{
            .label         = std::format("StagingBuffer_Texture_{}", _filepath),
            .usage         = EBufferUsage::TransferSrc,
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(imageSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    auto           *cmdBuf   = vkRender->beginIsolateCommands();
    VkCommandBuffer vkCmdBuf = cmdBuf->getHandleAs<VkCommandBuffer>();

    // Do layout transition image: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY
    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // For block compressed formats with multiple mip levels, need to copy each level separately
    // Use KTX API to get correct data offsets for each level
    bool isCompressed = isBlockCompressed(format);

    if (isCompressed && mipLevels > 1 && dataSize > 0) {
        // For KTX textures, we need to calculate offsets correctly
        // Each mip level is stored contiguously with proper alignment
        VkDeviceSize bufferOffset  = 0;
        uint32_t     currentWidth  = texWidth;
        uint32_t     currentHeight = texHeight;

        for (uint32_t level = 0; level < mipLevels; level++) {
            // Ensure minimum dimensions
            currentWidth  = std::max(currentWidth, 1u);
            currentHeight = std::max(currentHeight, 1u);

            // Calculate level size based on block compression
            size_t       blockSize = getFormatPixelSize(format);
            uint32_t     blocksX   = (currentWidth + 3) / 4;
            uint32_t     blocksY   = (currentHeight + 3) / 4;
            VkDeviceSize levelSize = blocksX * blocksY * blockSize;

            // Ensure we don't exceed buffer size
            if (bufferOffset + levelSize > imageSize) {
                YA_CORE_ERROR("Mip level {} data exceeds buffer size: {} > {}",
                              level,
                              bufferOffset + levelSize,
                              imageSize);
                break;
            }

            VkBufferImageCopy region{};
            region.bufferOffset                    = bufferOffset;
            region.bufferRowLength                 = 0; // Tightly packed
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = level;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount     = 1;
            region.imageOffset                     = {0, 0, 0};
            region.imageExtent                     = {currentWidth, currentHeight, 1};

            vkCmdCopyBufferToImage(vkCmdBuf,
                                   dynamic_cast<VulkanBuffer *>(stagingBuffer.get())->getHandle().as<VkBuffer>(),
                                   vkImage->getHandle().as<VkImage>(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &region);

            bufferOffset += levelSize;

            // Halve dimensions for next level
            currentWidth >>= 1;
            currentHeight >>= 1;
        }
    }
    else {
        // Simple copy for non-compressed or single mip level
        VulkanImage::transfer(vkCmdBuf, dynamic_cast<VulkanBuffer *>(stagingBuffer.get()), vkImage.get());
    }

    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkRender->endIsolateCommands(cmdBuf);

    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, vkImage->getHandle().as<VkImage>(), std::format("Texture_Image_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, vkImage->_imageMemory, std::format("Texture_ImageMemory_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, dynamic_cast<VulkanImageView *>(imageView.get())->getHandle().as<VkImageView>(), std::format("Texture_ImageView_{}", _filepath));
}

void Texture::createFallbackTexture(const void *pixels, size_t dataSize, uint32_t texWidth, uint32_t texHeight)
{
    _width     = texWidth;
    _height    = texHeight;
    _format    = EFormat::R8G8B8A8_UNORM;
    _mipLevels = 1;
    _channels  = 4;

    auto vkRender = ya::App::get()->getRender<VulkanRender>();

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
        .sharingMode   = ESharingMode::Exclusive,
        .initialLayout = EImageLayout::Undefined,
    };

    auto vkImage = VulkanImage::create(vkRender, ci);
    if (!vkImage) {
        YA_CORE_ERROR("Failed to create fallback texture!");
        return;
    }

    image     = vkImage;
    imageView = VulkanImageView::create(vkRender, vkImage, VK_IMAGE_ASPECT_COLOR_BIT);

    // Create staging buffer
    std::shared_ptr<IBuffer> stagingBuffer = IBuffer::create(
        vkRender,
        ya::BufferCreateInfo{
            .label         = std::format("StagingBuffer_Fallback_{}", _label),
            .usage         = EBufferUsage::TransferSrc,
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(dataSize),
            .memProperties = EMemoryProperty::HostVisible | EMemoryProperty::HostCoherent,
        });

    auto           *cmdBuf   = vkRender->beginIsolateCommands();
    VkCommandBuffer vkCmdBuf = cmdBuf->getHandleAs<VkCommandBuffer>();

    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanImage::transfer(vkCmdBuf, dynamic_cast<VulkanBuffer *>(stagingBuffer.get()), vkImage.get());
    VulkanImage::transitionLayout(vkCmdBuf, vkImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkRender->endIsolateCommands(cmdBuf);

    YA_CORE_WARN("Created fallback texture ({}x{}) for: {}", texWidth, texHeight, _filepath.empty() ? _label : _filepath);
}

} // namespace ya
