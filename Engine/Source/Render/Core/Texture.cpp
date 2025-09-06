#include "Texture.h"

#include "Core/App/App.h"
#include "Core/FileSystem/FileSystem.h"
#include "stb/stb_image.h"
#include <cstddef>


#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



namespace ya

{

Texture::Texture(const std::string &filepath)
{

    int   texWidth = -1, texHeight = -1, texChannels = -1;
    void *pixels = nullptr;

    // FileSystem::get()->getPluginRoots()
    pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        YA_CORE_ERROR("failed to load texture image! {}", filepath.data());
        return;
    }

    createImage(pixels, (uint32_t)texWidth, (uint32_t)texHeight);
    stbi_image_free(pixels);
}

Texture::Texture(uint32_t width, uint32_t height, const std::vector<ColorRGBA<uint8_t>> &data)
{
    YA_CORE_ASSERT((uint32_t)data.size() == width * height, "pixel data size does not match width * height");
    createImage(data.data(), width, height);
}

VkImage     Texture::getVkImage() { return image->_handle; }
VkImageView Texture::getVkImageView() { return imageView->_handle; }
VkFormat    Texture::getVkFormat() const { return toVk(_format); }

void Texture::createImage(const void *pixels, uint32_t texWidth, uint32_t texHeight)
{
    _width  = texWidth;
    _height = texHeight;

    auto         vkRender  = ya::App::get()->getRender<VulkanRender>();
    VkDeviceSize imageSize = sizeof(uint8_t) * texWidth * texHeight * 4;

    image = VulkanImage::create(
        vkRender,
        ya::ImageCreateInfo{
            .format = EFormat::R8G8B8A8_UNORM,
            .extent = {
                .width  = texWidth,
                .height = texHeight,
                .depth  = 1,
            },
            .mipLevels     = 1,
            .samples       = ESampleCount::Sample_1,
            .usage         = EImageUsage::Sampled | EImageUsage::TransferDst,
            .sharingMode   = ESharingMode::Exclusive,
            .initialLayout = EImageLayout::Undefined,
        });

    imageView = std::make_shared<VulkanImageView>(vkRender, image.get(), VK_IMAGE_ASPECT_COLOR_BIT);

    std::shared_ptr<VulkanBuffer> stagingBuffer = VulkanBuffer::create(
        vkRender,
        {
            .usage         = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // from buffer to image
            .data          = (void *)pixels,
            .size          = static_cast<uint32_t>(imageSize),
            .memProperties = 0,
            .debugName     = std::format("StagingBuffer_Texture_{}", _filepath),
        });

    auto cmdBuf = vkRender->beginIsolateCommands();
    // Do layout transition image: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY
    VulkanImage::transitionLayout(cmdBuf, image.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanImage::transfer(cmdBuf, stagingBuffer.get(), image.get());
    VulkanImage::transitionLayout(cmdBuf, image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkRender->endIsolateCommands(cmdBuf);
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, image->getHandle(), std::format("Texture_Image_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE, image->_imageMemory, std::format("Texture_ImageMemory_{}", _filepath));
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, imageView->getHandle(), std::format("Texture_ImageView_{}", _filepath));
}

} // namespace ya
