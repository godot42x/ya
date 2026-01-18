
#pragma once

#include <cstdint>
namespace refl
{

using type_index_t = uint32_t;

extern uint32_t _nextTypeID;

// 使用函数内静态变量（Meyers Singleton）避免静态初始化顺序问题
// inline uint32_t &getNextTypeId()
// {
//     static std::atomic<uint32_t> nextTypeId{1}; // 从 1 开始，0 保留为无效值
//     return *reinterpret_cast<uint32_t *>(&nextTypeId);
// }

template <typename T>
struct TypeIndex
{
    static uint32_t value()
    {
        static auto id = _nextTypeID++;
        return id;
        // 使用 typeid().hash_code() 作为类型 ID
        // 延迟求值：只在调用 value() 时才需要完整类型定义
        // return static_cast<uint32_t>(typeid(T).hash_code());
    }
};

// 注意：直接调用 TypeIndex<T>::value() 而不是使用变量模板
// 避免在不完整类型上实例化
template <typename T>
inline static const auto type_index_v = TypeIndex<T>::value();



} // namespace refl