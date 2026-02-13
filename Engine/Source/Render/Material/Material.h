#pragma once

#include "Core/Base.h"

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
struct TextureSlot
{
    YA_REFLECT_BEGIN(TextureSlot)
    YA_REFLECT_FIELD(textureRef)
    YA_REFLECT_FIELD(uvScale)
    YA_REFLECT_FIELD(uvOffset)
    YA_REFLECT_FIELD(uvRotation)
    YA_REFLECT_FIELD(bEnable)
    YA_REFLECT_END()

    TextureRef textureRef; // Serialized as path, auto-loaded on deserialize
    glm::vec2  uvScale{1.0f};
    glm::vec2  uvOffset{0.0f};
    float      uvRotation = 0.0f;
    bool       bEnable    = true;

    TextureSlot() = default;
    explicit TextureSlot(const std::string &path)
        : textureRef(path)
    {}

    /**
     * @brief Convert to runtime TextureView
     * @param defaultSampler Sampler to use (texture provides image, sampler is shared)
     */
    TextureView toTextureView(ya::Ptr<Sampler> sampler = nullptr) const
    {
        TextureView tv;
        tv.texture = textureRef.getShared();
        tv.sampler = sampler ? sampler : TextureLibrary::get().getDefaultSampler();
        tv.bEnable = bEnable;
        return tv;
    }

    /**
     * @brief Populate from an existing TextureView (for editor use)
     */
    void fromTextureView(const TextureView &tv, const std::string &texturePath)
    {
        textureRef.set(texturePath, tv.texture);
        bEnable = tv.bEnable;
    }

    bool resolve() { return textureRef.resolve(); }
    bool isLoaded() const { return textureRef.isLoaded(); }
    bool isValid() const { return textureRef.hasPath(); }

    void invalidate() { textureRef.invalidate(); }
};

/// Texture slot map type: maps resource enum (as int) to TextureSlot
using TextureSlotMap = std::unordered_map<int, TextureSlot>;

// struct TextureSlotMap2
// {
//     YA_REFLECT_BEGIN(TextureSlotMap2)
//     YA_REFLECT_FIELD(textureSlots)
//     YA_REFLECT_END()

//     TextureSlotMap textureSlots;

//     std::function<void()> onTextureSlotChanged;
// };


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
    YA_REFLECT_FIELD(_textureViews, .notSerialized())
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
    bool _bParamDirty    = true; ///< Material parameters changed
    bool _bResourceDirty = true; ///< Texture resources changed

    std::unordered_map<int, TextureView> _textureViews;
    // ========================================
    // Basic Accessors
    // ========================================
    std::string getLabel() const { return _label; }
    void        setLabel(const std::string &label) { _label = label; }

    [[nodiscard]] int32_t getIndex() const { return _instanceIndex; }
    void                  setIndex(int32_t index) { _instanceIndex = index; }

    [[nodiscard]] uint32_t getTypeID() const { return _typeID; }
    void                   setTypeID(const uint32_t &typeID) { _typeID = typeID; }

    // ========================================
    // Dirty Flags (Unified Interface)
    // ========================================
    void               setParamDirty(bool bInDirty = true) { _bParamDirty = bInDirty; }
    [[nodiscard]] bool isParamDirty() const { return _bParamDirty; }

    void               setResourceDirty(bool bInDirty = true) { _bResourceDirty = bInDirty; }
    [[nodiscard]] bool isResourceDirty() const { return _bResourceDirty; }

    // ========================================
    // Virtual Interface for Derived Classes
    // ========================================


    /**
     * @brief Get resource enum from slot name (for deserialization)
     * @param name The slot name from JSON
     * @return Resource enum value, or -1 if not found
     */
    virtual int getTextureSlotEnum(const std::string &name) const { return -1; }

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
    T *as()
    {
        static_assert(std::is_base_of<Material, T>::value, "T must be derived from Material");
        return static_cast<T *>(this);
    }
};

} // namespace ya