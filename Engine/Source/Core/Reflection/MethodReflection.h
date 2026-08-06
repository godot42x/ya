#pragma once

#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/InstanceRef.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/TypeIndex.h"

#include <reflects-core/lib.h>

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace ya::reflection::detail
{

// ============================================================================
// Application-layer JSON bridge for reflected functions.
//
// The plugin stays fully generic (std::any invoker + type metadata). The
// engine attaches its script-facing JSON callable to a reflected Function via
// an app-layer extension table keyed by the canonical Function object - an
// extension, not a duplicate function catalog.
// ============================================================================

using JsonInvoker = std::function<nlohmann::json(void*, const nlohmann::json&)>;

struct MethodJsonInvokers
{
    std::unordered_map<const ::Function*, JsonInvoker> table;

    static MethodJsonInvokers& get()
    {
        static MethodJsonInvokers instance;
        return instance;
    }
};

/**
 * @brief JsonMethodInvoker - typed <-> JSON conversion for reflected functions.
 *
 * Positional JSON array in, JSON result out (null for void). Friend of
 * ReflectionSerializer so it can reuse the generic value conversion
 * primitives (scalars, strings, enums, glm vectors via serialization hooks).
 */
struct JsonMethodInvoker
{
    template <typename... Args, size_t... Is>
    static void unpackArgs(std::tuple<std::decay_t<Args>...>& values,
                           const nlohmann::json&              args,
                           std::index_sequence<Is...>)
    {
        ((ReflectionSerializer::deserializeAnyValue(&std::get<Is>(values),
                                                    ya::type_index_v<std::decay_t<Args>>,
                                                    args[Is])),
         ...);
    }

    template <typename R>
    static nlohmann::json serializeReturn(R& value)
    {
        using Decayed = std::decay_t<R>;

        // Script facade methods may return a pre-built JSON document (e.g.
        // Entity::components()); pass it through unchanged.
        if constexpr (std::same_as<Decayed, nlohmann::json>) {
            return value;
        }
        // Handle-returning facade methods (component access): serialize to the
        // opaque handle marker the script binding materializes into a wrapper.
        if constexpr (std::same_as<Decayed, ::ya::InstanceRef>) {
            return serializeInstanceRef(value);
        }

        return ReflectionSerializer::serializeAnyValue(
            const_cast<void*>(static_cast<const void*>(std::addressof(value))),
            ya::type_index_v<Decayed>);
    }

    static nlohmann::json serializeReturnVoid()
    {
        return nlohmann::json(nullptr);
    }

    template <typename T, typename Ret, typename... Args>
    static nlohmann::json invokeMemberImpl(T& self, Ret (T::*fn)(Args...), const nlohmann::json& args)
    {
        std::tuple<std::decay_t<Args>...> values;
        unpackArgs<Args...>(values, args, std::index_sequence_for<Args...>{});
        if constexpr (std::is_void_v<Ret>) {
            std::apply([&](auto&&... v) { (self.*fn)(std::forward<decltype(v)>(v)...); }, values);
            return serializeReturnVoid();
        }
        else {
            return std::apply(
                [&](auto&&... v) -> nlohmann::json {
                    Ret&& result = (self.*fn)(std::forward<decltype(v)>(v)...);
                    return serializeReturn(result);
                },
                values);
        }
    }

    template <typename T, typename Ret, typename... Args>
    static nlohmann::json invokeMemberImpl(T& self, Ret (T::*fn)(Args...) const, const nlohmann::json& args)
    {
        std::tuple<std::decay_t<Args>...> values;
        unpackArgs<Args...>(values, args, std::index_sequence_for<Args...>{});
        if constexpr (std::is_void_v<Ret>) {
            std::apply([&](auto&&... v) { (self.*fn)(std::forward<decltype(v)>(v)...); }, values);
            return serializeReturnVoid();
        }
        else {
            return std::apply(
                [&](auto&&... v) -> nlohmann::json {
                    Ret&& result = (self.*fn)(std::forward<decltype(v)>(v)...);
                    return serializeReturn(result);
                },
                values);
        }
    }

    template <typename T, typename Fn>
    static nlohmann::json invokeMember(T& self, Fn fn, const nlohmann::json& args)
    {
        return invokeMemberImpl(self, fn, args);
    }
};

/**
 * @brief Fills the JSON call path of a plugin-registered Function.
 *
 * Called from the YA_REFLECT_METHOD expansion while the class is registered;
 * the reflected function stays stored in the plugin's Class::functions (single
 * source of truth) and this only completes its script-facing callable.
 */
template <typename T, typename Fn>
void registerReflectedMethod(::Function& fn, Fn member, const ::Metadata& metadata)
{
    fn.metadata = metadata;
    MethodJsonInvokers::get().table[&fn] =
        JsonInvoker{[member](void* self, const nlohmann::json& args) -> nlohmann::json {
            return JsonMethodInvoker::invokeMember(*static_cast<T*>(self), member, args);
        }};
}

/// Returns the script-facing callable attached to a reflected Function, or an
/// empty callable when the function was not registered through the engine
/// macros.
inline JsonInvoker findJsonInvoker(const ::Function& fn)
{
    auto& table = MethodJsonInvokers::get().table;
    const auto it = table.find(&fn);
    return it != table.end() ? it->second : JsonInvoker{};
}

} // namespace ya::reflection::detail
