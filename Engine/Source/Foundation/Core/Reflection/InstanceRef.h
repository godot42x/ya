#pragma once

#include "Foundation/Core/Api.h"
#include "Foundation/Core/TypeIndex.h"

#include <nlohmann/json.hpp>

namespace ya
{

/// JSON key of the opaque handle marker produced when a reflected method
/// returns an InstanceRef. Consumed by the script binding layer, which
/// materializes the marker back into a wrapped JS object.
inline constexpr const char* kInstanceRefJsonKey = "$yaHandle";

/**
 * @brief InstanceRef - a typed pointer to a reflected C++ instance.
 *
 * Engine facade methods that scripts must address "on the object" (component
 * access, entity/scene access) return an InstanceRef instead of a raw void*:
 * the concrete reflected type travels with the pointer, so the binding layer
 * can pick the right prototype without extra registry lookups.
 *
 * Wire format produced by serializeInstanceRef() (script bridge convention):
 *   {"$yaHandle": {"typeIndex": N, "ptr": <uintptr>}}
 */
struct InstanceRef
{
    ya::type_index_t typeIndex = 0;
    void*            instance  = nullptr;

    explicit operator bool() const { return instance != nullptr; }
};

inline nlohmann::json serializeInstanceRef(const InstanceRef& ref)
{
    if (ref.instance == nullptr) {
        return nullptr;
    }
    return nlohmann::json{
        {kInstanceRefJsonKey,
         {{"typeIndex", ref.typeIndex}, {"ptr", reinterpret_cast<uintptr_t>(ref.instance)}}}};
}

} // namespace ya
