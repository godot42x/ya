#include "Texture2DManager.h"

#include "Core/App/App.h"

#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanUtils.h"


// Static member definitions
std::unordered_map<FName, std::shared_ptr<Texture2DManager::Texture2D>> Texture2DManager::s_textureCache;
std::vector<std::shared_ptr<Texture2DManager::Texture2D>>               Texture2DManager::s_textureArray;
uint32_t                                                                Texture2DManager::s_nextTextureId = 1; // 0 reserved for white texture
std::shared_ptr<Texture2DManager::Texture2D>                            Texture2DManager::s_whiteTexture;
bool                                                                    Texture2DManager::s_initialized = false;

bool Texture2DManager::initialize()
{
    if (s_initialized) {
        NE_CORE_WARN("Texture2DManager already initialized");
        return true;
    }

    // Create the white texture (ID 0)
    if (!createWhiteTexture()) {
        NE_CORE_ERROR("Failed to create white texture");
        return false;
    }

    // Reserve space for texture array
    s_textureArray.resize(32); // Match max texture slots
    s_textureArray[0] = s_whiteTexture;

    s_initialized = true;
    NE_CORE_INFO("Texture2DManager initialized successfully");
    return true;
}

void Texture2DManager::shutdown()
{
    if (!s_initialized)
        return;

    auto app = ya::App::get();
    if (!app)
        return;

    auto _render = static_cast<VulkanRender *>(app->getRender());
    if (!_render)
        return;

    VkDevice device = _render->getLogicalDevice();
    vkDeviceWaitIdle(device);

    // Cleanup all textures
    for (auto &texture : s_textureArray) {
        if (texture && texture->isValid()) {
            vkDestroySampler(device, texture->sampler, nullptr);
            vkDestroyImageView(device, texture->imageView, nullptr);
            vkDestroyImage(device, texture->image, nullptr);
            vkFreeMemory(device, texture->memory, nullptr);
        }
    }

    s_textureCache.clear();
    s_textureArray.clear();
    s_whiteTexture.reset();
    s_nextTextureId = 1;
    s_initialized   = false;

    NE_CORE_INFO("Texture2DManager shutdown complete");
}

std::shared_ptr<Texture2DManager::Texture2D> Texture2DManager::loadTexture(const std::string &filePath)
{
    if (!s_initialized) {
        NE_CORE_ERROR("Texture2DManager not initialized");
        return nullptr;
    }

    FName pathName(filePath);

    // Check cache first
    auto it = s_textureCache.find(pathName);
    if (it != s_textureCache.end()) {
        return it->second;
    }

    // TODO: Load image from file using STB or similar
    // For now, create a placeholder colored texture
    NE_CORE_WARN("Texture loading from file not yet implemented: {}", filePath);

    // Create a simple colored texture as placeholder
    uint32_t colorData = 0xFFFFFFFF; // White
    auto     texture   = createTexture(&colorData, 1, 1, 4);

    if (texture) {
        s_textureCache[pathName] = texture;
    }

    return texture;
}

std::shared_ptr<Texture2DManager::Texture2D> Texture2DManager::getTexture(uint32_t textureId)
{
    if (textureId < s_textureArray.size()) {
        return s_textureArray[textureId];
    }
    return nullptr;
}

std::shared_ptr<Texture2DManager::Texture2D> Texture2DManager::getWhiteTexture()
{
    return s_whiteTexture;
}

std::shared_ptr<Texture2DManager::Texture2D> Texture2DManager::createTexture(const void *data, uint32_t width, uint32_t height, uint32_t channels)
{
    if (!s_initialized) {
        NE_CORE_ERROR("Texture2DManager not initialized");
        return nullptr;
    }

    return createVulkanTexture(data, width, height, channels);
}

std::vector<VkImageView> Texture2DManager::getAllTextureViews()
{
    std::vector<VkImageView> views;
    views.reserve(s_textureArray.size());

    for (const auto &texture : s_textureArray) {
        if (texture && texture->isValid()) {
            views.push_back(texture->imageView);
        }
        else {
            // Use white texture as fallback
            views.push_back(s_whiteTexture->imageView);
        }
    }

    return views;
}

std::vector<VkSampler> Texture2DManager::getAllTextureSamplers()
{
    std::vector<VkSampler> samplers;
    samplers.reserve(s_textureArray.size());

    for (const auto &texture : s_textureArray) {
        if (texture && texture->isValid()) {
            samplers.push_back(texture->sampler);
        }
        else {
            // Use white texture as fallback
            samplers.push_back(s_whiteTexture->sampler);
        }
    }

    return samplers;
}

bool Texture2DManager::createWhiteTexture()
{
    // Create 1x1 white texture
    uint32_t whitePixel = 0xFFFFFFFF; // RGBA white
    s_whiteTexture      = createVulkanTexture(&whitePixel, 1, 1, 4);

    if (s_whiteTexture) {
        s_whiteTexture->textureId = 0;
        return true;
    }

    return false;
}

std::shared_ptr<Texture2DManager::Texture2D> Texture2DManager::createVulkanTexture(const void *data, uint32_t width, uint32_t height, uint32_t channels)
{
    // auto app = ya::App::get();
    // if (!app)
    //     return nullptr;

    // auto _render = static_cast<VulkanRender *>(app->getRender());
    // if (!_render)
    //     return nullptr;

    // VkDevice         device         = _render->getLogicalDevice();
    // VkPhysicalDevice physicalDevice = _render->getPhysicalDevice();
    // VkCommandPool    commandPool    = VK_NULL_HANDLE;
    // // _render->getCommandPool();
    // UNIMPLEMENTED();
    // VkQueue graphicsQueue = nullptr; // _render->getGraphicsQueue();

    // auto texture       = std::make_shared<Texture2D>();
    // texture->width     = width;
    // texture->height    = height;
    // texture->channels  = channels;
    // texture->textureId = s_nextTextureId++;

    // // Convert channels to Vulkan format
    // VkFormat format = VK_FORMAT_R8G8B8A8_UNORM; // Default to RGBA
    // switch (channels) {
    // case 1:
    //     format = VK_FORMAT_R8_UNORM;
    //     break;
    // case 3:
    //     format = VK_FORMAT_R8G8B8_UNORM;
    //     break;
    // case 4:
    //     format = VK_FORMAT_R8G8B8A8_UNORM;
    //     break;
    // }

    // try {
    //     // Create image
    //     VulkanUtils::createImage(device, physicalDevice, width, height, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture->image, texture->memory);

    //     // Create image view
    //     texture->imageView = VulkanUtils::createImageView(device, texture->image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    //     // Create sampler
    //     VkSamplerCreateInfo samplerInfo{};
    //     samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    //     samplerInfo.magFilter               = VK_FILTER_LINEAR;
    //     samplerInfo.minFilter               = VK_FILTER_LINEAR;
    //     samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    //     samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    //     samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    //     samplerInfo.anisotropyEnable        = VK_FALSE;
    //     samplerInfo.maxAnisotropy           = 1.0f;
    //     samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    //     samplerInfo.unnormalizedCoordinates = VK_FALSE;
    //     samplerInfo.compareEnable           = VK_FALSE;
    //     samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
    //     samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    //     samplerInfo.mipLodBias              = 0.0f;
    //     samplerInfo.minLod                  = 0.0f;
    //     samplerInfo.maxLod                  = 0.0f;

    //     if (vkCreateSampler(device, &samplerInfo, nullptr, &texture->sampler) != VK_SUCCESS) {
    //         NE_CORE_ERROR("Failed to create texture sampler");
    //         return nullptr;
    //     }

    //     // Upload texture data if provided
    //     if (data) {
    //         VkDeviceSize imageSize = width * height * channels;

    //         // Create staging buffer
    //         VkBuffer       stagingBuffer;
    //         VkDeviceMemory stagingBufferMemory;
    //         VulkanUtils::createBuffer(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    //         // Copy data to staging buffer
    //         void *stagingData;
    //         vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &stagingData);
    //         memcpy(stagingData, data, static_cast<size_t>(imageSize));
    //         vkUnmapMemory(device, stagingBufferMemory);

    //         // Transition image layout and copy from buffer
    //         // VulkanUtils::transitionImageLayout(device, commandPool, graphicsQueue, texture->image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    //         VulkanUtils::copyBufferToImage(device, commandPool, graphicsQueue, stagingBuffer, texture->image, width, height);

    //         // VulkanUtils::transitionImageLayout(device, commandPool, graphicsQueue, texture->image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    //         // Cleanup staging buffer
    //         vkDestroyBuffer(device, stagingBuffer, nullptr);
    //         vkFreeMemory(device, stagingBufferMemory, nullptr);
    //     }

    //     // Add to texture array
    //     if (texture->textureId < s_textureArray.size()) {
    //         s_textureArray[texture->textureId] = texture;
    //     }

    //     NE_CORE_INFO("Created texture {}x{} with ID {}", width, height, texture->textureId);
    //     return texture;
    // }
    // catch (const std::exception &e) {
    //     NE_CORE_ERROR("Failed to create Vulkan texture: {}", e.what());
    //     return nullptr;
    // }
    return {};
}
