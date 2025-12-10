#pragma once

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <typeinfo>

// ============================================================================
// Field - Base for Property and Function
// ============================================================================
struct Field
{
    std::string name;
};

// ============================================================================
// MARK: Property
// Note: Reference types (T&) are not supported in the reflection system.
// If you need reference semantics, use pointer types (T*) instead.
// ============================================================================
struct Property : public Field
{
    std::any value; // 存储成员指针或静态变量指针
    bool     bConst   = false;
    bool     bStatic  = false;
    size_t   typeHash = 0; // 使用 typeid().hash_code() 作为类型标识

#ifdef _DEBUG
    std::string debugTypeName; // Debug only: 用于调试信息和错误提示
#endif

    // 用于从对象实例读取属性值
    std::function<std::any(void *)> getter;

    // 用于向对象实例写入属性值
    std::function<void(void *, const std::any &)> setter;

    // 检查属性是否为指定类型
    template <typename T>
    bool isType() const
    {
        return typeHash == typeid(T).hash_code();
    }

    // 获取类型名称（仅用于调试）
    std::string getTypeName() const
    {
#ifdef _DEBUG
        return debugTypeName;
#else
        return std::to_string(typeHash); // Release 模式无类型名
#endif
    }

    // 获取属性值（从对象实例）
    template <typename T>
    T getValue(void *obj) const
    {
        if (!getter) {
            throw std::runtime_error("No getter available for property: " + name);
        }
        return std::any_cast<T>(getter(obj));
    }

    // 设置属性值（到对象实例）
    template <typename T>
    void setValue(void *obj, const T &val)
    {
        if (bConst) {
            throw std::runtime_error("Cannot set const property: " + name);
        }
        if (!setter) {
            throw std::runtime_error("No setter available for property: " + name);
        }
        setter(obj, std::any(val));
    }

    // 旧的API保持兼容（用于静态值）
    template <typename T>
    T get() const
    {
        return std::any_cast<T>(value);
    }

    template <typename T>
    void set(const T &val)
    {
        if (bConst) {
            throw std::runtime_error("Cannot set const property: " + name);
        }
        value = val;
    }
};
