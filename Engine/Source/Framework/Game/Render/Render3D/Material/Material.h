#pragma once

#include "Core/Common/TextureSlot.h"

#include <atomic>
#include <string>

namespace ya
{

namespace detail
{

/**
 * @brief Global monotonically-increasing version counter for material dirty tracking.
 *
 * Every newly constructed Material gets a unique initial version for both its
 * param and resource versions.  This guarantees that MaterialDescPool (which
 * caches the last-uploaded version per slot) will always detect a version
 * mismatch when a new material occupies an existing slot — even if the pool
 * was not rebuilt in between.
 */
inline uint64_t nextMaterialVersion()
{
    static std::atomic<uint64_t> s_counter{0};
    return s_counter.fetch_add(1, std::memory_order_relaxed) + 1; // 1-based
}

} // namespace detail

/**
 * @brief Serializable texture slot for material serialization
 * Stores texture path and UV transform parameters
 */
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
    uint64_t _paramVersion    = detail::nextMaterialVersion();
    uint64_t _resourceVersion = detail::nextMaterialVersion();

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
