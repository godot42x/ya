#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <memory>
#include <string>

/**
 * @brief Simple texture management for 2D rendering
 * Handles loading and caching of 2D textures for the F2DRender system
 */
class Texture2DManager {
public:
    struct Texture2D {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        
        uint32_t width{0};
        uint32_t height{0};
        uint32_t channels{4};
        
        uint32_t textureId{0}; // Unique ID for shader binding
        
        bool isValid() const {
            return image != VK_NULL_HANDLE && imageView != VK_NULL_HANDLE;
        }
    };

private:
    static std::unordered_map<FName, std::shared_ptr<Texture2D>> s_textureCache;
    static std::vector<std::shared_ptr<Texture2D>> s_textureArray;
    static uint32_t s_nextTextureId;
    static std::shared_ptr<Texture2D> s_whiteTexture;
    static bool s_initialized;

public:
    /**
     * @brief Initialize the texture manager
     */
    static bool initialize();
    
    /**
     * @brief Shutdown and cleanup all textures
     */
    static void shutdown();
    
    /**
     * @brief Load a texture from file
     * @param filePath Path to the texture file
     * @return Shared pointer to the loaded texture, or nullptr if failed
     */
    static std::shared_ptr<Texture2D> loadTexture(const std::string& filePath);
    
    /**
     * @brief Get texture by ID
     * @param textureId The texture ID
     * @return Shared pointer to the texture, or nullptr if not found
     */
    static std::shared_ptr<Texture2D> getTexture(uint32_t textureId);
    
    /**
     * @brief Get the white texture (used for solid color rendering)
     * @return The white texture
     */
    static std::shared_ptr<Texture2D> getWhiteTexture();
    
    /**
     * @brief Create a texture from raw data
     * @param data Raw pixel data
     * @param width Texture width
     * @param height Texture height  
     * @param channels Number of channels (1, 3, or 4)
     * @return Shared pointer to the created texture
     */
    static std::shared_ptr<Texture2D> createTexture(const void* data, uint32_t width, uint32_t height, uint32_t channels = 4);
    
    /**
     * @brief Get all texture image views for descriptor set binding
     * @return Vector of image views for all loaded textures
     */
    static std::vector<VkImageView> getAllTextureViews();
    
    /**
     * @brief Get all texture samplers for descriptor set binding
     * @return Vector of samplers for all loaded textures
     */
    static std::vector<VkSampler> getAllTextureSamplers();

private:
    /**
     * @brief Create the default white texture
     */
    static bool createWhiteTexture();
    
    /**
     * @brief Create Vulkan texture resources
     */
    static std::shared_ptr<Texture2D> createVulkanTexture(const void* data, uint32_t width, uint32_t height, uint32_t channels);
};
