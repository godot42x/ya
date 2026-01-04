
#pragma once

#include "Core/Base.h"
#include <nlohmann/json.hpp>

namespace ya::reflection::detail
{
// 前向声明 ExternalReflect
template <typename T>
struct ExternalReflect;
} // namespace ya::reflection::detail

namespace ya
{

namespace serializer_detail
{
// 检测是否支持外部反射
template <typename T, typename = void>
struct has_external_reflect : std::false_type
{};

template <typename T>
struct has_external_reflect<T, std::void_t<
                                   decltype(::ya::reflection::detail::ExternalReflect<T>::visit_properties(
                                       std::declval<T &>(),
                                       std::declval<int (*)(const char *, int &)>()))>> : std::true_type
{};

template <typename T>
inline constexpr bool has_external_reflect_v = has_external_reflect<T>::value;
} // namespace serializer_detail

/**
 * @brief 基于 visitor 模式的通用序列化器
 *
 * 核心思想：
 * 1. 通过反射宏生成的 __visit_properties() 遍历所有属性
 * 2. 序列化器提供 visitor lambda，访问每个属性
 * 3. 支持嵌套序列化（递归访问）
 *
 * @example 序列化
 * TransformComponent comp;
 * auto json = ReflectionSerializer::serialize(comp);
 *
 * @example 反序列化
 * TransformComponent comp = ReflectionSerializer::deserialize<TransformComponent>(json);
 */
struct ReflectionSerializer
{
    /**
     * 序列化：通过 visitor 遍历属性并转换为 JSON
     */
    template <typename T>
        requires requires(T obj) { obj.__visit_properties(std::declval<decltype([](const char *, auto &) {})>()); }
    static nlohmann::json serialize(const T &obj)
    {
        nlohmann::json j;

        // 创建序列化 visitor
        auto visitor = [&j](const char *name, auto &value) {
            using ValueType = std::decay_t<decltype(value)>;

            // 递归序列化嵌套对象（侵入式反射）
            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                j[name] = serialize(value);
            }
            // 递归序列化嵌套对象（非侵入式反射 - 使用 ExternalReflect）
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                j[name] = serializeExternal(value);
            }
            // 基础类型直接赋值（nlohmann::json 支持的类型）
            else {
                j[name] = value;
            }
        };

        // 使用 const_cast 允许对 const 对象调用 visitor
        const_cast<T &>(obj).__visit_properties(visitor);

        return j;
    }

    /**
     * 序列化外部反射类型（非侵入式）
     */
    template <typename T>
        requires serializer_detail::has_external_reflect_v<T>
    static nlohmann::json serializeExternal(const T &obj)
    {
        nlohmann::json j;

        auto visitor = [&j](const char *name, auto &value) {
            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                j[name] = serialize(value);
            }
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                j[name] = serializeExternal(value);
            }
            else {
                j[name] = value;
            }
        };

        ::ya::reflection::detail::ExternalReflect<T>::visit_properties(const_cast<T &>(obj), visitor);

        return j;
    }

    /**
     * 反序列化：从 JSON 通过 visitor 设置属性
     */
    template <typename T>
        requires requires(T obj) { obj.__visit_properties(std::declval<decltype([](const char *, auto &) {})>()); }
    static T deserialize(const nlohmann::json &j)
    {
        T obj{};

        // 创建反序列化 visitor
        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            // 递归反序列化嵌套对象（侵入式反射）
            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                value = deserialize<ValueType>(j[name]);
            }
            // 递归反序列化嵌套对象（非侵入式反射）
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                value = deserializeExternal<ValueType>(j[name]);
            }
            // 基础类型直接赋值
            else {
                value = j[name].get<ValueType>();
            }
        };

        obj.__visit_properties(visitor);

        return obj;
    }

    /**
     * 反序列化外部反射类型（非侵入式）
     */
    template <typename T>
        requires serializer_detail::has_external_reflect_v<T>
    static T deserializeExternal(const nlohmann::json &j)
    {
        T obj{};

        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                value = deserialize<ValueType>(j[name]);
            }
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                value = deserializeExternal<ValueType>(j[name]);
            }
            else {
                value = j[name].get<ValueType>();
            }
        };

        ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, visitor);

        return obj;
    }

    /**
     * 就地反序列化：直接修改现有对象
     */
    template <typename T>
        requires requires(T obj) { obj.__visit_properties(std::declval<decltype([](const char *, auto &) {})>()); }
    static void deserializeInPlace(T &obj, const nlohmann::json &j)
    {
        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            // 递归反序列化嵌套对象（侵入式反射）
            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                deserializeInPlace(value, j[name]);
            }
            // 递归反序列化嵌套对象（非侵入式反射）
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                deserializeInPlaceExternal(value, j[name]);
            }
            // 基础类型直接赋值
            else {
                value = j[name].get<ValueType>();
            }
        };

        obj.__visit_properties(visitor);
    }

    /**
     * 就地反序列化外部反射类型（非侵入式）
     */
    template <typename T>
        requires serializer_detail::has_external_reflect_v<T>
    static void deserializeInPlaceExternal(T &obj, const nlohmann::json &j)
    {
        auto visitor = [&j](const char *name, auto &value) {
            if (!j.contains(name)) return;

            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (requires { value.__visit_properties(std::declval<void(*)(const char*, int&)>()); }) {
                deserializeInPlace(value, j[name]);
            }
            else if constexpr (serializer_detail::has_external_reflect_v<ValueType>) {
                deserializeInPlaceExternal(value, j[name]);
            }
            else {
                value = j[name].get<ValueType>();
            }
        };

        ::ya::reflection::detail::ExternalReflect<T>::visit_properties(obj, visitor);
    }
};

} // namespace ya
