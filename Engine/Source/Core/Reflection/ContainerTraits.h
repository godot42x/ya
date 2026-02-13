/**
 * @file ContainerTraits.h
 * @brief 容器类型特征 - 支持标准库容器的反射
 *
 * 设计思路：
 * 1. 使用模板特化识别容器类型
 * 2. 提供统一的容器元数据接口
 * 3. 支持嵌套容器（vector<vector<T>>）
 */

#pragma once

#include <array>
#include <map>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya::reflection
{

// ============================================================================
// 容器类型枚举
// ============================================================================

enum class ContainerCategory
{
    None,
    SequenceContainer,    // vector, deque, list
    AssociativeContainer, // map, set
    UnorderedContainer    // unordered_map, unordered_set
};

enum class EContainer
{
    None,
    Vector,
    Deque,
    List,
    Map,
    Set,
    UnorderedMap,
    UnorderedSet,
    Array
};

// ============================================================================
// 容器特征基类
// ============================================================================

template <typename T>
struct ContainerTraits
{
    static constexpr bool              IsContainer = false;
    static constexpr EContainer        Type        = EContainer::None;
    static constexpr ContainerCategory Category    = ContainerCategory::None;
    static constexpr bool              HasKeyValue = false;
};

// ============================================================================
// Vector 特化
// ============================================================================

template <typename T, typename Allocator>
struct ContainerTraits<std::vector<T, Allocator>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::Vector;
    static constexpr ContainerCategory Category    = ContainerCategory::SequenceContainer;
    static constexpr bool              HasKeyValue = false;

    using ElementType   = T;
    using ContainerType = std::vector<T, Allocator>;
    using Iterator      = typename std::vector<T, Allocator>::iterator;
    using ConstIterator = typename std::vector<T, Allocator>::const_iterator;
};

// ============================================================================
// Map 特化
// ============================================================================

template <typename K, typename V, typename Compare, typename Allocator>
struct ContainerTraits<std::map<K, V, Compare, Allocator>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::Map;
    static constexpr ContainerCategory Category    = ContainerCategory::AssociativeContainer;
    static constexpr bool              HasKeyValue = true;

    using KeyType       = K;
    using ValueType     = V;
    using ElementType   = std::pair<const K, V>;
    using ContainerType = std::map<K, V, Compare, Allocator>;
    using Iterator      = typename std::map<K, V, Compare, Allocator>::iterator;
    using ConstIterator = typename std::map<K, V, Compare, Allocator>::const_iterator;
};

// ============================================================================
// Set 特化
// ============================================================================

template <typename T, typename Compare, typename Allocator>
struct ContainerTraits<std::set<T, Compare, Allocator>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::Set;
    static constexpr ContainerCategory Category    = ContainerCategory::AssociativeContainer;
    static constexpr bool              HasKeyValue = false;

    using ElementType   = T;
    using ContainerType = std::set<T, Compare, Allocator>;
    using Iterator      = typename std::set<T, Compare, Allocator>::iterator;
    using ConstIterator = typename std::set<T, Compare, Allocator>::const_iterator;
};

// ============================================================================
// UnorderedMap 特化
// ============================================================================

template <typename K, typename V, typename Hash, typename KeyEqual, typename Allocator>
struct ContainerTraits<std::unordered_map<K, V, Hash, KeyEqual, Allocator>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::UnorderedMap;
    static constexpr ContainerCategory Category    = ContainerCategory::UnorderedContainer;
    static constexpr bool              HasKeyValue = true;

    using KeyType       = K;
    using ValueType     = V;
    using ElementType   = std::pair<const K, V>;
    using ContainerType = std::unordered_map<K, V, Hash, KeyEqual, Allocator>;
    using Iterator      = typename std::unordered_map<K, V, Hash, KeyEqual, Allocator>::iterator;
    using ConstIterator = typename std::unordered_map<K, V, Hash, KeyEqual, Allocator>::const_iterator;
};

// ============================================================================
// UnorderedSet 特化
// ============================================================================

template <typename T, typename Hash, typename KeyEqual, typename Allocator>
struct ContainerTraits<std::unordered_set<T, Hash, KeyEqual, Allocator>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::UnorderedSet;
    static constexpr ContainerCategory Category    = ContainerCategory::UnorderedContainer;
    static constexpr bool              HasKeyValue = false;

    using ElementType   = T;
    using ContainerType = std::unordered_set<T, Hash, KeyEqual, Allocator>;
    using Iterator      = typename std::unordered_set<T, Hash, KeyEqual, Allocator>::iterator;
    using ConstIterator = typename std::unordered_set<T, Hash, KeyEqual, Allocator>::const_iterator;
};

// ============================================================================
// Array 特化 (fixed-size container)
// ============================================================================

template <typename T, std::size_t N>
struct ContainerTraits<std::array<T, N>>
{
    static constexpr bool              IsContainer = true;
    static constexpr EContainer        Type        = EContainer::Array;
    static constexpr ContainerCategory Category    = ContainerCategory::SequenceContainer;
    static constexpr bool              HasKeyValue = false;
    static constexpr std::size_t       Size        = N;

    using ElementType   = T;
    using ContainerType = std::array<T, N>;
    using Iterator      = typename std::array<T, N>::iterator;
    using ConstIterator = typename std::array<T, N>::const_iterator;
};

// ============================================================================
// 便捷的检测宏
// ============================================================================

template <typename T>
inline constexpr bool is_container_v = ContainerTraits<T>::IsContainer;

template <typename T>
inline constexpr bool is_sequence_container_v =
    ContainerTraits<T>::Category == ContainerCategory::SequenceContainer;

template <typename T>
inline constexpr bool is_associative_container_v =
    ContainerTraits<T>::Category == ContainerCategory::AssociativeContainer;

template <typename T>
inline constexpr bool is_map_like_v = ContainerTraits<T>::HasKeyValue;

// ============================================================================
// 嵌套容器检测
// ============================================================================

template <typename T>
struct is_nested_container : std::false_type
{};

template <typename T, typename Allocator>
struct is_nested_container<std::vector<T, Allocator>>
    : std::bool_constant<is_container_v<T>>
{};

template <typename T>
inline constexpr bool is_nested_container_v = is_nested_container<T>::value;

} // namespace ya::reflection
