/**
 * @brief 运行时反射桥接 - 连接 YA_REFLECT 宏和 reflects-core 系统
 *
 * 这个文件负责在编译期通过宏将静态反射信息注册到 reflects-core 的运行时反射系统中，
 * 从而实现 O(1) 的属性查找，而不是每次都使用 visitor 模式进行 O(n) 遍历。
 *
 * 重要：直接使用 reflects-core 的 Register<T> 模板，避免重复实现。
 * 同时合并元数据注册，避免重复调用。
 */

#pragma once

#include "Core/Common/FWD-std.h"
#include <reflects-core/lib.h>



namespace ya::reflection::detail
{

/**
 * @brief 辅助函数：从运行时反射系统获取属性值
 *
 * 这是一个便捷函数，用于从对象中获取指定属性的值。
 *
 * @tparam T 属性类型
 * @param obj 对象指针
 * @param className 类名
 * @param propName 属性名
 * @return std::optional<T> 如果属性存在且类型匹配，返回值；否则返回空
 */
template <typename T>
std::optional<T> getRuntimePropertyValue(void *obj, const std::string &className, const std::string &propName)
{
    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(className);

    if (!classPtr) {
        return std::nullopt;
    }

    auto *prop = classPtr->getProperty(propName);
    if (!prop) {
        return std::nullopt;
    }

    try {
        std::any value = prop->getter(obj);
        return std::any_cast<T>(value);
    }
    catch (const std::bad_any_cast &) {
        return std::nullopt;
    }
}

/**
 * @brief 辅助函数：向运行时反射系统设置属性值
 *
 * 这是一个便捷函数，用于设置对象的指定属性值。
 *
 * @tparam T 属性类型
 * @param obj 对象指针
 * @param className 类名
 * @param propName 属性名
 * @param value 要设置的值
 * @return bool 是否成功设置
 */
template <typename T>
bool setRuntimePropertyValue(void *obj, const std::string &className, const std::string &propName, const T &value)
{
    auto &registry = ClassRegistry::instance();
    auto *classPtr = registry.getClass(className);

    if (!classPtr) {
        return false;
    }

    auto *prop = classPtr->getProperty(propName);
    if (!prop) {
        return false;
    }

    try {
        prop->setter(obj, std::any(value));
        return true;
    }
    catch (...) {
        return false;
    }
}

} // namespace ya::reflection::detail
