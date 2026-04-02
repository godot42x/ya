#pragma once

#include "Core/Common/AssetRef.h"
#include "Render/Core/Texture.h"

#include <unordered_map>

#include "Resource/TextureLibrary.h"
namespace ya
{

/**
 * @brief Serializable texture slot for material serialization
 * Stores texture path and UV transform parameters
 */
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
    YA_REFLECT_FIELD(uvScale)
    YA_REFLECT_FIELD(uvOffset)
    YA_REFLECT_FIELD(uvRotation)
    YA_REFLECT_FIELD(bEnable)
    YA_REFLECT_FIELD(samplerConfig)
    YA_REFLECT_END()

    TextureRef    textureRef; // Serialized as path, auto-loaded on deserialize
    glm::vec2     uvScale{1.0f};
    glm::vec2     uvOffset{0.0f};
    float         uvRotation = 0.0f;
    bool          bEnable    = true;
    SamplerConfig samplerConfig; ///< Optional sampler override (default uses global sampler)

    TextureSlot() = default;
    explicit TextureSlot(const std::string& path)
        : textureRef(path)
    {}

    // ========================================
    // Resolved Resource Accessors
    // ========================================

    /**
     * @brief Get the resolved Texture pointer (valid after resolve()).
     * @return Shared ptr to Texture, or nullptr if not yet resolved.
     */
    [[nodiscard]] ya::Ptr<Texture> getResolvedTexture() const
    {
        if (textureRef.isLoaded()) {
            return textureRef.getShared();
        }

        if (!textureRef.hasPath()) {
            return TextureLibrary::get().getWhiteTexture();
        }

        return nullptr;
    }

    /**
     * @brief Get the resolved Sampler for this slot.
     *
     * If samplerConfig is default, returns TextureLibrary::getDefaultSampler().
     * Otherwise creates/reuses a sampler matching the config.
     */
    [[nodiscard]] ya::Ptr<Sampler> getResolvedSampler() const
    {
        // TODO: When custom sampler creation is implemented, check samplerConfig here.
        // For now, always return default sampler (backward compatible).
        return TextureLibrary::get().getDefaultSampler();
    }

    /**
     * @brief Build a TextureBinding from this slot's resolved resources.
     */
    [[nodiscard]] TextureBinding toTextureBinding() const
    {
        TextureBinding tb;
        tb.texture = getResolvedTexture();
        tb.sampler = getResolvedSampler();
        return tb;
    }

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
    bool               isEditorEnableEditable() const { return hasPath(); }
    bool               isEnabledEffective() const { return hasPath() && textureRef.isLoaded() && textureRef.get() != nullptr && bEnable; }

    // Legacy compatibility accessors. Prefer hasPath()/isReady()/needsResolve().
    bool isLoaded() const { return isReady(); }
    bool isValid() const { return isReady(); }

    void invalidate()
    {
        textureRef.invalidate();
    }
};

/// Texture slot map type: maps resource enum (as int) to TextureSlot
using TextureSlotMap = std::unordered_map<int, TextureSlot>;


//  MARK: Material
/**
 * @brief Material base class - Serializable material data
 *
 * Design:
 * - Component layer holds Material reference (serializable path/params)
 * - Material class is the actual data storage
 * - ResourceResolveSystem handles all resource loading
 *
 * Derived classes should:
 * 1. Define their EResource enum for texture slots
 * 2. Define ParamUBO struct for uniform parameters
 * 3. Override getTextureSlotName() for serialization
 */
struct Material
{
    YA_REFLECT_BEGIN(Material)
    YA_REFLECT_FIELD(_label)
    YA_REFLECT_FIELD(_instanceIndex)
    YA_REFLECT_FIELD(_typeID)
    YA_REFLECT_FIELD(_sourcePath)
    YA_REFLECT_END()

    // ========================================
    // Serializable Data
    // ========================================
    std::string _label         = "MaterialNone";
    uint32_t    _typeID        = 0;
    std::string _sourcePath    = "";
    int32_t     _instanceIndex = -1;

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    bool     _bParamDirty     = true; ///< Compatibility flag for legacy callers
    bool     _bResourceDirty  = true; ///< Compatibility flag for legacy callers
    uint64_t _paramVersion    = 1;
    uint64_t _resourceVersion = 1;

    std::string getLabel() const { return _label; }
    void        setLabel(const std::string& label) { _label = label; }

    [[nodiscard]] int32_t getIndex() const { return _instanceIndex; }
    void                  setIndex(int32_t index) { _instanceIndex = index; }

    [[nodiscard]] uint32_t getTypeID() const { return _typeID; }
    void                   setTypeID(const uint32_t& typeID) { _typeID = typeID; }

    void setParamDirty(bool bInDirty = true)
    {
        _bParamDirty = bInDirty;
        if (bInDirty) {
            ++_paramVersion;
        }
    }
    [[nodiscard]] bool     isParamDirty() const { return _bParamDirty; }
    [[nodiscard]] uint64_t getParamVersion() const { return _paramVersion; }

    void setResourceDirty(bool bInDirty = true)
    {
        _bResourceDirty = bInDirty;
        if (bInDirty) {
            ++_resourceVersion;
        }
    }
    [[nodiscard]] bool     isResourceDirty() const { return _bResourceDirty; }
    [[nodiscard]] uint64_t getResourceVersion() const { return _resourceVersion; }



    /**
     * @brief Get resource enum from slot name (for deserialization)
     * @param name The slot name from JSON
     * @return Resource enum value, or -1 if not found
     */
    virtual int getTextureSlotEnum(const std::string& name) const
    {
        (void)name;
        return -1;
    }

    /**
     * @brief Resolve all texture resources
     * @return true if all resources resolved successfully
     */
    virtual bool resolveTextures() { return true; }

    virtual ~Material() = default;

    // ========================================
    // Type Casting Helper
    // ========================================
    template <typename T>
    T* as()
    {
        static_assert(std::is_base_of<Material, T>::value, "T must be derived from Material");
        return static_cast<T*>(this);
    }
};

} // namespace ya
