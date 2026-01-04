#pragma once

#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>



struct Metadata
{
    std::string name;
    uint32_t    flags = 0;

    // 额外的元数据
    std::unordered_map<std::string, std::any> metadata;

    // 便捷方法
    template <typename T>
    void set(const std::string &key, const T &value)
    {
        metadata[key] = value;
    }

    template <typename T>
    T get(const std::string &key, const T &defaultValue = T{}) const
    {
        auto it = metadata.find(key);
        if (it != metadata.end()) {
            try {
                return std::any_cast<T>(it->second);
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    bool hasMeta(const std::string &key) const
    {
        return metadata.find(key) != metadata.end();
    }

    // 检查是否有任何元数据（用于判断是否需要注册）
    bool hasAnyMetadata() const
    {
        return flags != 0 || !metadata.empty();
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
// Field - Base for Property and Function (包含元数据支持)
// ============================================================================

enum class FieldFlags : uint32_t
{
    None               = 0,
    EditAnywhere       = 1 << 0, // 可在编辑器中编辑
    EditReadOnly       = 1 << 1, // 只读
    NotSerialized      = 1 << 2, // 不序列化
    Transient          = 1 << 3, // 临时变量（不保存）
    Category           = 1 << 4, // 分类
    Replicated         = 1 << 5, // 网络复制
    BlueprintReadOnly  = 1 << 6,
    BlueprintReadWrite = 1 << 7,
    // Function 特定的标记
    BlueprintCallable = 1 << 8,  // 可从蓝图调用
    BlueprintPure     = 1 << 9,  // 纯函数（无副作用）
    Exec              = 1 << 10, // 执行函数
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
    // 元数据支持 - Property 和 Function 共享
    // ============================================================================

    Metadata &getMetadata()
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
    std::any value; // 存储成员指针或静态变量指针
    bool     bConst    = false;
    bool     bStatic   = false;
    size_t   typeHash  = 0; // 使用 typeid().hash_code() 作为类型标识
    uint32_t typeIndex = 0;

#ifdef _DEBUG
    std::string debugTypeName; // Debug only: 用于调试信息和错误提示
#endif

    // ============================================================================
    // 反射运行时功能
    // ============================================================================

    // 用于从对象实例读取属性值
    std::function<std::any(void *)> getter;

    // 用于获取属性在对象中的地址（支持复杂类型递归序列化）
    // 返回指向成员的指针；对静态属性该值可为空。
    std::function<const void *(const void *)> addressGetter;

    // 用于获取属性在对象中的可写地址（用于反序列化递归写回）
    // 仅对非 const 的普通成员有效；静态/const 成员可为空。
    std::function<void *(void *)> addressGetterMutable;

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
