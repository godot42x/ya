#pragma once

#include "Core/Api.h"

#include <reflects-core/lib.h>

#include <nlohmann/json.hpp>
#include <string>

namespace ya::reflection
{

struct FEqualFacet
{
    bool (*equals)(const void* left, const void* right) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

/// A reflected value struct whose fields cross to scripts. The script layer
/// copies the whole value and lets each language backend project the fields.
/// It does not name the struct.
struct FValueStruct
{
    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

struct FPrintFacet
{
    std::string (*print)(const refl::TypeOps& ops, const void* value) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Fallback;

    static YA_CORE_API const FPrintFacet& fallback();
};

/// Value JSON. The serializer asks with `facet` and, when this is absent and
/// the value is neither an enum nor a class, logs and skips the field.
struct FJsonFacet
{
    nlohmann::json (*serialize)(const void* value) = nullptr;
    void (*deserialize)(void* value, const nlohmann::json& json) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

/// Component document. Missing means reflection only. `serialize` runs after
/// the reflection object (or instead of it when `bUseReflection` is false)
/// and appends into the same object.
struct FComponentJsonFacet
{
    bool bUseReflection = true;
    void (*serialize)(const void* component, nlohmann::json& out) = nullptr;
    void (*deserialize)(void* component, const nlohmann::json& in) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

/// 32-bit integer read/write used by the inspector. Only types that register
/// this facet are integers; a 32-bit `long` on Windows is not one of them.
struct FIntegerFacet
{
    bool bSigned = true;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

namespace detail
{

template <class T>
bool equalValue(const void* left, const void* right)
{
    return *static_cast<const T*>(left) == *static_cast<const T*>(right);
}

template <class T>
struct ValuePrintBinder
{
    static inline std::string (*typed)(const T&) = nullptr;

    static std::string call(const refl::TypeOps&, const void* value)
    {
        return typed(*static_cast<const T*>(value));
    }
};

} // namespace detail

template <class T>
void registerEqualFacet()
{
    refl::TypeRegistry::instance().setFacet<FEqualFacet>(
        refl::type_index_v<T>,
        FEqualFacet{.equals = &detail::equalValue<T>});
}

template <class T>
void markValueStruct()
{
    refl::TypeRegistry::instance().setFacet<FValueStruct>(refl::type_index_v<T>, FValueStruct{});
}

template <class T>
void registerPrintFacet(std::string (*print)(const T&))
{
    detail::ValuePrintBinder<T>::typed = print;
    refl::TypeRegistry::instance().setFacet<FPrintFacet>(
        refl::type_index_v<T>,
        FPrintFacet{.print = &detail::ValuePrintBinder<T>::call});
}

namespace detail
{

template <class T>
nlohmann::json writeJsonValue(const void* value)
{
    return *static_cast<const T*>(value);
}

template <class T>
void readJsonValue(void* value, const nlohmann::json& json)
{
    *static_cast<T*>(value) = json.get<T>();
}

} // namespace detail

template <class T>
void registerJsonFacet()
{
    refl::TypeRegistry::instance().setFacet<FJsonFacet>(
        refl::type_index_v<T>,
        FJsonFacet{
            .serialize   = &detail::writeJsonValue<T>,
            .deserialize = &detail::readJsonValue<T>,
        });
}

template <class T>
void registerJsonFacet(nlohmann::json (*serialize)(const void*), void (*deserialize)(void*, const nlohmann::json&))
{
    refl::TypeRegistry::instance().setFacet<FJsonFacet>(
        refl::type_index_v<T>,
        FJsonFacet{.serialize = serialize, .deserialize = deserialize});
}

template <class T>
void registerIntegerFacet(bool bSigned)
{
    refl::TypeRegistry::instance().setFacet<FIntegerFacet>(
        refl::type_index_v<T>,
        FIntegerFacet{.bSigned = bSigned});
}

} // namespace ya::reflection
