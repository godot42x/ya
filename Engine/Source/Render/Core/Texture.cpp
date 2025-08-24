#include "Texture.h"
#include "Core/App/App.h"
#include "Core/FileSystem/FileSystem.h"
#include "stb/stb_image.h"

#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



namespace ya

{

Texture::Texture(const std::string &filepath)
{

    auto vkRender = ya::App::get()->getRender<VulkanRender>();

    int   texWidth = -1, texHeight = -1, texChannels = -1;
    void *pixels = nullptr;

    // FileSystem::get()->getPluginRoots()

#if USE_SDL_IMG
    // SDL implementation
#else
    pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        YA_CORE_ERROR("failed to load texture image! {}", filepath.data());
        return;
    }
#endif


    VkDeviceSize imageSize = sizeof(uint8_t) * texWidth * texHeight * 4;

    image = VulkanImage::create(
        vkRender,
        ya::ImageCreateInfo{
            .format = EFormat::R8G8B8A8_UNORM,
            .extent = {
                .width  = static_cast<uint32_t>(texWidth),
                .height = static_cast<uint32_t>(texHeight),
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
            .debugName     = std::format("StagingBuffer_Texture_{}", filepath),
        });

#if USE_SDL_IMG
    // SDL cleanup
#else
    stbi_image_free(pixels);
#endif



    auto cmdBuf = vkRender->beginIsolateCommands();
    // Do layout transition image: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY
    VulkanImage::transitionLayout(cmdBuf, image.get(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VulkanImage::transfer(cmdBuf, stagingBuffer.get(), image.get());
    VulkanImage::transitionLayout(cmdBuf, image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkRender->endIsolateCommands(cmdBuf);
}

VkImage     Texture::getVkImage() { return image->_handle; }
VkImageView Texture::getVkImageView() { return imageView->_handle; }

} // namespace ya
