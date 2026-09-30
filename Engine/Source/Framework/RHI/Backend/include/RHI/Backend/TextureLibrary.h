#pragma once

#include "Core/Base.h"
#include "Core/ResourceRegistry.h"
#include "RHI/Core/BuiltinTextureSource.h"
#include "RHI/Core/Sampler.h"
#include "RHI/Core/Texture.h"
#include <array>
#include <memory>



namespace ya
{

struct IRender;

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
class YA_RHI_BACKEND_API TextureLibrary : public IResourceCache, public IBuiltinTextureSource
{
  public:
    static TextureLibrary &get();

    /**
     * @brief Initialize the texture library
     * Must be called before using any textures
     */
    void init(IRender* render);

    // IResourceCache interface
    void  clearCache() override;
    FName getCacheName() const override { return "TextureLibrary"; }

    // IBuiltinTextureSource
    void shutdown() override;

    /**
     * @brief Get a 1x1 white texture (RGBA: 255,255,255,255)
     */
    std::shared_ptr<Texture> getWhiteTexture() override;

    /**
     * @brief Get a 1x1 black texture (RGBA: 0,0,0,255)
     */
    ya::Ptr<Texture> getBlackTexture();

    /**
     * @brief Get a 2x2 multi-pixel test texture
     * Layout: white, blue, blue, white
     */
    ya::Ptr<Texture> getMultiPixelTexture();

    /**
     * @brief Get an 8x8 checkerboard texture (purple/black alternating)
     * Used as default fallback for missing or failed textures
     */
    ya::Ptr<Texture> getCheckerboardTexture();

    /**
     * @brief Get a 1x1 flat normal (RGB 128, 128, 255)
     * Semantic default for normal-map slots while their texture loads
     */
    ya::Ptr<Texture> getFlatNormalTexture();

    /**
     * @brief Get the default sampler (linear filtering)
     */
    std::shared_ptr<Sampler> getDefaultSampler() override;

    /**
     * @brief Get a linear filtering sampler
     */
    ya::Ptr<Sampler> getLinearSampler();

    /**
     * @brief Get a clamp-to-edge linear sampler for UI/font atlases.
     */
    ya::Ptr<Sampler> getClampLinearSampler();

    /**
     * @brief Get a nearest filtering sampler
     */
    ya::Ptr<Sampler> getNearestSampler();

    /**
     * @brief Clamp-to-edge nearest sampler for pixel-aligned bitmap/coverage font
     * atlases. Bitmap glyphs are rasterized at display size and should snap to
     * integer pixels; nearest filtering keeps strokes crisp, while the border
     * baked around each glyph prevents atlas neighbor bleed.
     */
    ya::Ptr<Sampler> getClampNearestSampler();

    /**
     * @brief The sampler a texture slot's SamplerConfig asks for. Linear ones
     * filter anisotropically. Cubic filters need a device extension the engine
     * does not enable; they sample linearly.
     */
    ya::Ptr<Sampler> getSampler(EFilter::T filter, ESamplerAddressMode::T addressMode);

  public:
    TextureLibrary()  = default;
    ~TextureLibrary() = default;

    // Non-copyable
    TextureLibrary(const TextureLibrary &)            = delete;
    TextureLibrary &operator=(const TextureLibrary &) = delete;

    void createSamplers(IRender* render);
    void createTextures(IRender* render);

    // Textures
    std::shared_ptr<Texture> _whiteTexture;
    std::shared_ptr<Texture> _blackTexture;
    std::shared_ptr<Texture> _multiPixelTexture;
    std::shared_ptr<Texture> _checkerboardTexture;
    std::shared_ptr<Texture> _flatNormalTexture;

    // Samplers
    std::shared_ptr<Sampler> _defaultSampler;
    std::shared_ptr<Sampler> _linearSampler;
    std::shared_ptr<Sampler> _clampLinearSampler;
    std::shared_ptr<Sampler> _nearestSampler;
    std::shared_ptr<Sampler> _clampNearestSampler;
    /// [nearest, linear][address mode]; _linearSampler is the linear/repeat entry.
    std::array<std::array<std::shared_ptr<Sampler>, 4>, 2> _slotSamplers;

    bool _initialized = false;
};

} // namespace ya
