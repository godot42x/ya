#pragma once

#include "Core/Api.h"

#include <reflects-core/lib.h>

#include <string>

namespace ya::reflection
{

struct FEqualFacet
{
    bool (*equals)(const void* left, const void* right) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
};

struct FPrintFacet
{
    std::string (*print)(const refl::TypeOps& ops, const void* value) = nullptr;

    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Fallback;

    static YA_CORE_API const FPrintFacet& fallback();
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
void registerPrintFacet(std::string (*print)(const T&))
{
    detail::ValuePrintBinder<T>::typed = print;
    refl::TypeRegistry::instance().setFacet<FPrintFacet>(
        refl::type_index_v<T>,
        FPrintFacet{.print = &detail::ValuePrintBinder<T>::call});
}

} // namespace ya::reflection
