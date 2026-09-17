#pragma once

// ============================================================================
// TextureSlot: serializable material texture-slot descriptor.
//
// Pure authoring/resolve data (texture ref + UV transform + optional sampler
// config). GPU-facing conversions (white-texture fallback, TextureBinding)
// are implemented by the consuming layers (GUI texture library / render
// adapters), not here, so this header stays below RHI.
// ============================================================================

#include "Core/Common/AssetRef.h"
#include "Core/Common/SamplerEnums.h"
#include "Core/Reflection/Reflection.h"

#include <glm/glm.hpp>

namespace ya
{

/**
 * @brief Optional sampler configuration for a texture slot.
 *
 * When present, a custom Sampler is created/reused at resolve time.
 * When absent (default-constructed), the global default sampler is used.
 */
struct SamplerConfig
{
    YA_REFLECT_BEGIN(SamplerConfig)
    YA_REFLECT_FIELD(filterMode)
    YA_REFLECT_FIELD(addressMode)
    YA_REFLECT_END()

    EFilter::T             filterMode  = EFilter::Linear;
    ESamplerAddressMode::T addressMode = ESamplerAddressMode::Repeat;

    [[nodiscard]] bool isDefault() const { return filterMode == EFilter::Linear && addressMode == ESamplerAddressMode::Repeat; }
};

/**
 * @brief Serializable texture slot for material serialization
 * Stores texture path, UV transform parameters, and optional sampler config.
 */
struct TextureSlot
{
    YA_REFLECT_BEGIN(TextureSlot)
    YA_REFLECT_FIELD(textureRef)
    YA_REFLECT_FIELD(bEnable, .editableIf<class_t>(&class_t::isEnableEditable, "empty slot"))
    YA_REFLECT_FIELD(uvScale)
    YA_REFLECT_FIELD(uvOffset)
    YA_REFLECT_FIELD(uvRotation)
    YA_REFLECT_FIELD(samplerConfig)
    YA_REFLECT_END()

    TextureRef    textureRef; // Serialized as path, auto-loaded on deserialize
    bool          bEnable = true;
    glm::vec2     uvScale{1.0f};
    glm::vec2     uvOffset{0.0f};
    float         uvRotation = 0.0f;
    SamplerConfig samplerConfig; ///< Optional sampler override (default uses global sampler)

    TextureSlot() = default;
    explicit TextureSlot(const std::string& path)
        : textureRef(path)
    {}

    // ========================================
    // Path / Resolve helpers
    // ========================================

    void fromPath(const std::string& path)
    {
        textureRef.set(path, nullptr);
    }

    EAssetResolveResult resolve()
    {
        if (!textureRef.hasPath()) {
            return EAssetResolveResult::Ready;
        }
        return textureRef.resolve();
    }
    bool               isLoading() const { return textureRef.isLoading(); }
    bool               hasPath() const { return textureRef.hasPath(); }
    bool               isReady() const { return !textureRef.hasPath() || textureRef.isLoaded(); }
    bool               needsResolve() const { return textureRef.hasPath() && !textureRef.isLoaded(); }
    EAssetResolveState getResolveState() const { return !textureRef.hasPath() ? EAssetResolveState::Ready : textureRef.getResolveState(); }
    bool               isEnableEditable() const { return hasPath(); }
    bool               isEnabledEffective() const { return hasPath() && textureRef.isLoaded() && textureRef.get() != nullptr && bEnable; }

    // Legacy compatibility accessors. Prefer hasPath()/isReady()/needsResolve().
    bool isLoaded() const { return isReady(); }
    bool isValid() const { return isReady(); }

    void invalidate()
    {
        textureRef.invalidate();
    }
};

} // namespace ya
