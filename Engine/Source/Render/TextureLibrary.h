#pragma once

#include "Core/ResourceRegistry.h"
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
 *   TextureLibrary::get().init();
 *   auto whiteTexture = TextureLibrary::get().getWhiteTexture();
 */
class TextureLibrary : public IResourceCache
{
public:
    static TextureLibrary& get()
    {
        static TextureLibrary instance;
        return instance;
    }

    /**
     * @brief Initialize the texture library
     * Must be called before using any textures
     */
    void init();

    // IResourceCache interface
    void clearCache() override;
    const char* getCacheName() const override { return "TextureLibrary"; }

    /**
     * @brief Get a 1x1 white texture (RGBA: 255,255,255,255)
     */
    std::shared_ptr<Texture> getWhiteTexture();

    /**
     * @brief Get a 1x1 black texture (RGBA: 0,0,0,255)
     */
    std::shared_ptr<Texture> getBlackTexture();

    /**
     * @brief Get a 2x2 multi-pixel test texture
     * Layout: white, blue, blue, white
     */
    std::shared_ptr<Texture> getMultiPixelTexture();

    /**
     * @brief Get the default sampler (linear filtering)
     */
    std::shared_ptr<Sampler> getDefaultSampler();

    /**
     * @brief Get a linear filtering sampler
     */
    std::shared_ptr<Sampler> getLinearSampler();

    /**
     * @brief Get a nearest filtering sampler
     */
    std::shared_ptr<Sampler> getNearestSampler();

private:
    TextureLibrary() = default;
    ~TextureLibrary() = default;
    
    // Non-copyable
    TextureLibrary(const TextureLibrary&) = delete;
    TextureLibrary& operator=(const TextureLibrary&) = delete;

    void createSamplers();
    void createTextures();

    // Textures
    std::shared_ptr<Texture> _whiteTexture;
    std::shared_ptr<Texture> _blackTexture;
    std::shared_ptr<Texture> _multiPixelTexture;

    // Samplers
    std::shared_ptr<Sampler> _defaultSampler;
    std::shared_ptr<Sampler> _linearSampler;
    std::shared_ptr<Sampler> _nearestSampler;

    bool _initialized = false;
};

} // namespace ya
