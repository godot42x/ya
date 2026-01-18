#pragma once

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <variant>

#include "type_index.h"



struct Metadata
{
    std::string name;
    uint32_t    flags = 0;

    std::unordered_map<std::string, std::any> metas;

    // Set simple types (stored in variant)
    template <typename T>
    void set(const std::string &key, const T &value)
    {
        metas[key] = value;
    }

    // Get complex types (from std::any)
    template <typename T>
    T get(const std::string &key) const
    {
        auto it = metas.find(key);
        if (it != metas.end()) {
            return std::any_cast<T>(it->second);
        }
        throw std::runtime_error("Metadata key not found: " + key);
    }

    bool hasMeta(const std::string &key) const
    {
        return metas.find(key) != metas.end();
    }

    // Check if any metadata exists (used to determine if registration is needed)
    bool hasAnyMetadata() const
    {
        return flags != 0 || !metas.empty();
    }

    bool hasFlag(uint32_t flag) const
    {
        return (flags & flag) != 0;
    }
    template <typename T>
        requires std::is_enum_v<T>
    bool hasFlag(T flag) const
    {
        return (flags & static_cast<uint32_t>(flag)) != 0;
    }
};



// ============================================================================
// Field - Base for Property and Function (includes metadata support)
// ============================================================================

enum class FieldFlags : uint32_t
{
    None               = 0,
    EditAnywhere       = 1 << 0, // Editable in editor
    EditReadOnly       = 1 << 1, // Read-only
    NotSerialized      = 1 << 2, // Don't serialize
    Transient          = 1 << 3, // Temporary variable (not saved)
    Category           = 1 << 4, // Category
    Replicated         = 1 << 5, // Network replication
    BlueprintReadOnly  = 1 << 6,
    BlueprintReadWrite = 1 << 7,
    // Function-specific flags
    BlueprintCallable = 1 << 8,  // Can be called from Blueprint
    BlueprintPure     = 1 << 9,  // Pure function (no side effects)
    Exec              = 1 << 10, // Execution function
};

inline FieldFlags operator|(FieldFlags a, FieldFlags b)
{
    return static_cast<FieldFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(FieldFlags a, FieldFlags b)
{
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}
struct Field
{
    std::string name;
    Metadata    metadata;

    // ============================================================================
    // Metadata support - shared by Property and Function
    // ============================================================================

    Metadata &getMetadata()
    {
        return metadata;
    }
    const Metadata &getMetadata() const
    {
        return metadata;
    }
};

// ============================================================================
// MARK: Property
// Note: Reference types (T&) are not supported in the reflection system.
// If you need reference semantics, use pointer types (T*) instead.
// ============================================================================
struct Property : public Field
{
    bool        bConst    = false;
    bool        bStatic   = false;
    uint32_t    typeIndex = 0;
    std::string typeName;

    // ============================================================================
    // Runtime Reflection - Pointer-based access (no std::any)
    // ============================================================================

    // Get const pointer to the property's address in an object
    // For static properties, obj can be nullptr
    std::function<const void *(const void *)> addressGetter;

    // Get mutable pointer to the property's address in an object
    // Only valid for non-const members; nullptr for static/const members
    std::function<void *(void *)> addressGetterMutable;

    // Get type name (for debugging only)
    std::string getTypeName() const
    {
        return typeName;
    }

    // Get property value (typed access via pointer)
    template <typename T>
    T getValue(const void *obj) const
    {
        if (!addressGetter) {
            throw std::runtime_error("No addressGetter available for property: " + name);
        }
        const void *addr = addressGetter(obj);
        if (!addr) {
            throw std::runtime_error("addressGetter returned null for property: " + name);
        }
        return *static_cast<const T *>(addr);
    }

    // Set property value (typed access via pointer)
    template <typename T>
    void setValue(void *obj, const T &val)
    {
        if (bConst) {
            throw std::runtime_error("Cannot set const property: " + name);
        }
        if (!addressGetterMutable) {
            throw std::runtime_error("No addressGetterMutable available for property: " + name);
        }
        void *addr = addressGetterMutable(obj);
        if (!addr) {
            throw std::runtime_error("addressGetterMutable returned null for property: " + name);
        }
        *static_cast<T *>(addr) = val;
    }

    // Get raw const pointer to property
    const void *getAddress(const void *obj) const
    {
        return addressGetter ? addressGetter(obj) : nullptr;
    }

    // Get raw mutable pointer to property
    void *getMutableAddress(void *obj) const
    {
        return addressGetterMutable ? addressGetterMutable(obj) : nullptr;
    }

    template <typename T>
    bool isType() const
    {
        return typeIndex == refl::type_index_v<T>;
    }
};
