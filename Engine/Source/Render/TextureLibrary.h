#pragma once

#include "Render/Core/Texture.h"
#include "Render/Core/Sampler.h"
#include <memory>

namespace ya
{

/**
 * @brief TextureLibrary - Manages common textures and samplers
 * 
 * Responsibilities:
 * - Provide standard textures (white, black, etc.)
 * - Manage common samplers (linear, nearest)
 * - Lazy initialization of resources
 * 
 * Usage:
 *   TextureLibrary::init();
 *   auto whiteTexture = TextureLibrary::getWhiteTexture();
 *   TextureLibrary::destroy();
 */
class TextureLibrary
{
public:
    /**
     * @brief Initialize the texture library
     * Must be called before using any textures
     */
    static void init();

    /**
     * @brief Cleanup all textures and samplers
     */
    static void destroy();

    /**
     * @brief Get a 1x1 white texture (RGBA: 255,255,255,255)
     */
    static std::shared_ptr<Texture> getWhiteTexture();

    /**
     * @brief Get a 1x1 black texture (RGBA: 0,0,0,255)
     */
    static std::shared_ptr<Texture> getBlackTexture();

    /**
     * @brief Get a 2x2 multi-pixel test texture
     * Layout: white, blue, blue, white
     */
    static std::shared_ptr<Texture> getMultiPixelTexture();

    /**
     * @brief Get the default sampler (linear filtering)
     */
    static std::shared_ptr<Sampler> getDefaultSampler();

    /**
     * @brief Get a linear filtering sampler
     */
    static std::shared_ptr<Sampler> getLinearSampler();

    /**
     * @brief Get a nearest filtering sampler
     */
    static std::shared_ptr<Sampler> getNearestSampler();

private:
    TextureLibrary() = default;
    ~TextureLibrary() = default;

    static void createSamplers();
    static void createTextures();

    // Textures
    static std::shared_ptr<Texture> s_whiteTexture;
    static std::shared_ptr<Texture> s_blackTexture;
    static std::shared_ptr<Texture> s_multiPixelTexture;

    // Samplers
    static std::shared_ptr<Sampler> s_defaultSampler;
    static std::shared_ptr<Sampler> s_linearSampler;
    static std::shared_ptr<Sampler> s_nearestSampler;

    static bool s_initialized;
};

} // namespace ya
