/**
 * @file ContainerProperty.h
 * @brief 容器属性抽象 - 提供统一的容器访问接口
 *
 * 设计思路：
 * 1. 类似 Unreal 的 FArrayProperty/FMapProperty
 * 2. 提供统一的迭代器接口
 * 3. 类型擦除，运行时多态
 * 4. 集成到反射系统，无需单独序列化代码
 */

#pragma once

#include "ContainerTraits.h"
#include "Core/TypeIndex.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ya::reflection
{

// ============================================================================
// 容器访问接口（类似 Unreal 的 FProperty）
// ============================================================================

/**
 * @brief 容器元素迭代器
 * 提供类型擦除的迭代访问
 */
struct ContainerIterator
{
    virtual ~ContainerIterator()                 = default;
    virtual bool     hasNext() const             = 0;
    virtual void     next()                      = 0;
    virtual void    *getElementPtr()             = 0; // 返回当前元素的指针
    virtual uint32_t getElementTypeIndex() const = 0;
    virtual void    *getKeyPtr() { return nullptr; }       // Map 专用：获取当前 key 指针
    virtual uint32_t getKeyTypeIndex() const { return 0; } // Map 专用：获取 key 类型索引
};

/**
 * @brief 容器操作接口
 * 统一的容器访问抽象（类似 Unreal FArrayProperty）
 */
struct IContainerProperty
{
    virtual ~IContainerProperty() = default;

    // 基础信息
    virtual EContainer        getContainerType() const = 0;
    virtual ContainerCategory getCategory() const      = 0;
    virtual bool              isMapLike() const        = 0;

    // 容器操作
    virtual size_t getSize(void *containerPtr) const       = 0;
    virtual void   clear(void *containerPtr)               = 0;
    virtual void   resize(void *containerPtr, size_t size) = 0; // 仅 Vector 等支持

    // 元素访问
    virtual void    *getElementPtr(void *containerPtr, size_t index)         = 0; // Vector[i]
    virtual void    *getValuePtr(void *containerPtr, const std::string &key) = 0; // Map[key]
    virtual uint32_t getElementTypeIndex() const                             = 0;
    virtual uint32_t getKeyTypeIndex() const { return 0; } // Map 专用

    // 迭代器
    virtual std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) = 0;

    // 添加/删除（可选）
    virtual void addElement(void *containerPtr, void *elementPtr) {}                // Vector::push_back
    virtual void removeElement(void *containerPtr, size_t index) {}                 // Vector::erase
    virtual void removeByKey(void *containerPtr, void *keyPtr) {}                   // Map::erase (keyPtr 指向实际的 key)
    virtual void insertElement(void *containerPtr, void *keyPtr, void *valuePtr) {} // Map::insert
};

// ============================================================================
// MARK: Vector 容器属性实现
// ============================================================================

template <typename T, typename Allocator = std::allocator<T>>
class VectorProperty : public IContainerProperty
{
    using ContainerType = std::vector<T, Allocator>;
    using ElementType   = T;

    // Vector 迭代器实现
    struct VectorIterator : public ContainerIterator
    {
        ContainerType *container;
        size_t         currentIndex = 0;

        VectorIterator(ContainerType *c) : container(c) {}

        bool hasNext() const override { return currentIndex < container->size(); }
        void next() override { ++currentIndex; }

        void *getElementPtr() override
        {
            return currentIndex < container->size() ? &(*container)[currentIndex] : nullptr;
        }

        uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }
    };

  public:
    EContainer        getContainerType() const override { return EContainer::Vector; }
    ContainerCategory getCategory() const override { return ContainerCategory::SequenceContainer; }
    bool              isMapLike() const override { return false; }

    size_t getSize(void *containerPtr) const override
    {
        return static_cast<ContainerType *>(containerPtr)->size();
    }

    void clear(void *containerPtr) override
    {
        static_cast<ContainerType *>(containerPtr)->clear();
    }

    void resize(void *containerPtr, size_t size) override
    {
        static_cast<ContainerType *>(containerPtr)->resize(size);
    }

    void *getElementPtr(void *containerPtr, size_t index) override
    {
        auto *vec = static_cast<ContainerType *>(containerPtr);
        return index < vec->size() ? &(*vec)[index] : nullptr;
    }

    void *getValuePtr(void *containerPtr, const std::string &key) override
    {
        return nullptr; // Vector 不支持 key 访问
    }

    uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }

    std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) override
    {
        return std::make_unique<VectorIterator>(static_cast<ContainerType *>(containerPtr));
    }

    void addElement(void *containerPtr, void *elementPtr) override
    {
        auto *vec = static_cast<ContainerType *>(containerPtr);
        if (elementPtr) {
            // 使用拷贝构造函数创建新元素
            vec->emplace_back(*static_cast<const T *>(elementPtr));
        }
        else {
            vec->emplace_back(); // 默认构造
        }
    }

    void removeElement(void *containerPtr, size_t index) override
    {
        auto *vec = static_cast<ContainerType *>(containerPtr);
        if (index < vec->size()) {
            vec->erase(vec->begin() + index);
        }
    }
};

// ============================================================================
// MARK: Map 容器属性实现
// ============================================================================

template <typename K, typename V>
class MapProperty : public IContainerProperty
{
    using ContainerType = std::map<K, V>;
    using KeyType       = K;
    using ValueType     = V;

    // Map 迭代器实现
    struct MapIterator : public ContainerIterator
    {
        ContainerType                   *container;
        typename ContainerType::iterator current;

        MapIterator(ContainerType *c) : container(c), current(c->begin()) {}

        bool hasNext() const override { return current != container->end(); }
        void next() override { ++current; }

        void *getElementPtr() override
        {
            return current != container->end() ? &current->second : nullptr;
        }

        uint32_t getElementTypeIndex() const override { return ya::type_index_v<V>; }

        void *getKeyPtr() override
        {
            return current != container->end() ? const_cast<K *>(&current->first) : nullptr;
        }

        uint32_t getKeyTypeIndex() const override { return ya::type_index_v<K>; }
    };

  public:
    EContainer        getContainerType() const override { return EContainer::Map; }
    ContainerCategory getCategory() const override { return ContainerCategory::AssociativeContainer; }
    bool              isMapLike() const override { return true; }

    size_t getSize(void *containerPtr) const override
    {
        return static_cast<ContainerType *>(containerPtr)->size();
    }

    void clear(void *containerPtr) override
    {
        static_cast<ContainerType *>(containerPtr)->clear();
    }

    void resize(void *containerPtr, size_t size) override
    {
        // Map 不支持 resize
    }

    void *getElementPtr(void *containerPtr, size_t index) override
    {
        // Map 不支持索引访问
        return nullptr;
    }

    void *getValuePtr(void *containerPtr, const std::string &key) override
    {
        auto *map = static_cast<ContainerType *>(containerPtr);

        K actualKey;
        if constexpr (std::is_same_v<K, std::string>) {
            actualKey = key;
        }
        else if constexpr (std::is_same_v<K, int>) {
            actualKey = std::stoi(key);
        }
        else if constexpr (std::is_same_v<K, long>) {
            actualKey = std::stol(key);
        }
        else if constexpr (std::is_same_v<K, long long>) {
            actualKey = std::stoll(key);
        }
        else if constexpr (std::is_same_v<K, float>) {
            actualKey = std::stof(key);
        }
        else if constexpr (std::is_same_v<K, double>) {
            actualKey = std::stod(key);
        }
        else {
            return nullptr; // 不支持的 key 类型
        }

        auto it = map->find(actualKey);
        return it != map->end() ? &it->second : nullptr;
    }

    uint32_t getElementTypeIndex() const override { return ya::type_index_v<V>; }
    uint32_t getKeyTypeIndex() const override { return ya::type_index_v<K>; }

    std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) override
    {
        return std::make_unique<MapIterator>(static_cast<ContainerType *>(containerPtr));
    }

    void insertElement(void *containerPtr, void *keyPtr, void *valuePtr) override
    {
        if (!keyPtr) return;

        auto    *map = static_cast<ContainerType *>(containerPtr);
        const K &key = *static_cast<K *>(keyPtr);

        if (valuePtr) {
            (*map)[key] = *static_cast<V *>(valuePtr);
        }
        else {
            (*map)[key] = V{}; // 默认构造
        }
    }

    void removeByKey(void *containerPtr, void *keyPtr) override
    {
        if (!keyPtr) return;

        auto    *map = static_cast<ContainerType *>(containerPtr);
        const K &key = *static_cast<K *>(keyPtr);
        map->erase(key);
    }
};

// ============================================================================
// MARK: Set 容器属性实现
// ============================================================================

template <typename T, typename Compare = std::less<T>, typename Allocator = std::allocator<T>>
class SetProperty : public IContainerProperty
{
    using ContainerType = std::set<T, Compare, Allocator>;
    using ElementType   = T;

    // Set 迭代器实现
    struct SetIterator : public ContainerIterator
    {
        ContainerType                   *container;
        typename ContainerType::iterator current;

        SetIterator(ContainerType *c) : container(c), current(c->begin()) {}

        bool hasNext() const override { return current != container->end(); }
        void next() override { ++current; }

        void *getElementPtr() override
        {
            return current != container->end() ? const_cast<T *>(&(*current)) : nullptr;
        }

        uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }
    };

  public:
    EContainer        getContainerType() const override { return EContainer::Set; }
    ContainerCategory getCategory() const override { return ContainerCategory::AssociativeContainer; }
    bool              isMapLike() const override { return false; }

    size_t getSize(void *containerPtr) const override
    {
        return static_cast<ContainerType *>(containerPtr)->size();
    }

    void clear(void *containerPtr) override
    {
        static_cast<ContainerType *>(containerPtr)->clear();
    }

    void resize(void *containerPtr, size_t size) override
    {
        // Set 不支持 resize
    }

    void *getElementPtr(void *containerPtr, size_t index) override
    {
        // Set 不支持索引访问
        return nullptr;
    }

    void *getValuePtr(void *containerPtr, const std::string &key) override
    {
        return nullptr; // Set 不支持 key 访问
    }

    uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }

    std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) override
    {
        return std::make_unique<SetIterator>(static_cast<ContainerType *>(containerPtr));
    }

    void addElement(void *containerPtr, void *elementPtr) override
    {
        auto *set = static_cast<ContainerType *>(containerPtr);
        if (elementPtr) {
            set->insert(*static_cast<T *>(elementPtr));
        }
    }

    void removeByKey(void *containerPtr, void *keyPtr) override
    {
        if (!keyPtr) return;

        auto    *set = static_cast<ContainerType *>(containerPtr);
        const T &key = *static_cast<T *>(keyPtr);
        set->erase(key);
    }
};

// ============================================================================
// MARK: UnorderedMap 容器属性实现
// ============================================================================

template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class UnorderedMapProperty : public IContainerProperty
{
    using ContainerType = std::unordered_map<K, V, Hash, KeyEqual>;
    using KeyType       = K;
    using ValueType     = V;

    // UnorderedMap 迭代器实现
    struct UnorderedMapIterator : public ContainerIterator
    {
        ContainerType                   *container;
        typename ContainerType::iterator current;

        UnorderedMapIterator(ContainerType *c) : container(c), current(c->begin()) {}

        bool hasNext() const override { return current != container->end(); }
        void next() override { ++current; }

        void *getElementPtr() override
        {
            return current != container->end() ? &current->second : nullptr;
        }

        uint32_t getElementTypeIndex() const override { return ya::type_index_v<V>; }

        void *getKeyPtr() override
        {
            return current != container->end() ? const_cast<K *>(&current->first) : nullptr;
        }

        uint32_t getKeyTypeIndex() const override { return ya::type_index_v<K>; }
    };

  public:
    EContainer        getContainerType() const override { return EContainer::UnorderedMap; }
    ContainerCategory getCategory() const override { return ContainerCategory::UnorderedContainer; }
    bool              isMapLike() const override { return true; }

    size_t getSize(void *containerPtr) const override
    {
        return static_cast<ContainerType *>(containerPtr)->size();
    }

    void clear(void *containerPtr) override
    {
        static_cast<ContainerType *>(containerPtr)->clear();
    }

    void resize(void *containerPtr, size_t size) override
    {
        // UnorderedMap 不支持 resize
    }

    void *getElementPtr(void *containerPtr, size_t index) override
    {
        // UnorderedMap 不支持索引访问
        return nullptr;
    }

    void *getValuePtr(void *containerPtr, const std::string &key) override
    {
        auto *map = static_cast<ContainerType *>(containerPtr);

        K actualKey;
        if constexpr (std::is_same_v<K, std::string>) {
            actualKey = key;
        }
        else if constexpr (std::is_same_v<K, int>) {
            actualKey = std::stoi(key);
        }
        else if constexpr (std::is_same_v<K, long>) {
            actualKey = std::stol(key);
        }
        else if constexpr (std::is_same_v<K, long long>) {
            actualKey = std::stoll(key);
        }
        else if constexpr (std::is_same_v<K, float>) {
            actualKey = std::stof(key);
        }
        else if constexpr (std::is_same_v<K, double>) {
            actualKey = std::stod(key);
        }
        else {
            return nullptr; // 不支持的 key 类型
        }

        auto it = map->find(actualKey);
        return it != map->end() ? &it->second : nullptr;
    }

    uint32_t getElementTypeIndex() const override { return ya::type_index_v<V>; }
    uint32_t getKeyTypeIndex() const override { return ya::type_index_v<K>; }

    std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) override
    {
        return std::make_unique<UnorderedMapIterator>(static_cast<ContainerType *>(containerPtr));
    }

    void insertElement(void *containerPtr, void *keyPtr, void *valuePtr) override
    {
        if (!keyPtr) return;

        auto    *map = static_cast<ContainerType *>(containerPtr);
        const K &key = *static_cast<K *>(keyPtr);

        if (valuePtr) {
            (*map)[key] = *static_cast<V *>(valuePtr);
        }
        else {
            (*map)[key] = V{}; // 默认构造
        }
    }

    void removeByKey(void *containerPtr, void *keyPtr) override
    {
        if (!keyPtr) return;

        auto    *map = static_cast<ContainerType *>(containerPtr);
        const K &key = *static_cast<K *>(keyPtr);
        map->erase(key);
    }
};

// ============================================================================
// MARK: UnorderedSet 容器属性实现
// ============================================================================

template <typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
class UnorderedSetProperty : public IContainerProperty
{
    using ContainerType = std::unordered_set<T, Hash, KeyEqual>;
    using ElementType   = T;

    // UnorderedSet 迭代器实现
    struct UnorderedSetIterator : public ContainerIterator
    {
        ContainerType                   *container;
        typename ContainerType::iterator current;

        UnorderedSetIterator(ContainerType *c) : container(c), current(c->begin()) {}

        bool hasNext() const override { return current != container->end(); }
        void next() override { ++current; }

        void *getElementPtr() override
        {
            return current != container->end() ? const_cast<T *>(&(*current)) : nullptr;
        }

        uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }
    };

  public:
    EContainer        getContainerType() const override { return EContainer::UnorderedSet; }
    ContainerCategory getCategory() const override { return ContainerCategory::UnorderedContainer; }
    bool              isMapLike() const override { return false; }

    size_t getSize(void *containerPtr) const override
    {
        return static_cast<ContainerType *>(containerPtr)->size();
    }

    void clear(void *containerPtr) override
    {
        static_cast<ContainerType *>(containerPtr)->clear();
    }

    void resize(void *containerPtr, size_t size) override
    {
        // UnorderedSet 不支持 resize
    }

    void *getElementPtr(void *containerPtr, size_t index) override
    {
        // UnorderedSet 不支持索引访问
        return nullptr;
    }

    void *getValuePtr(void *containerPtr, const std::string &key) override
    {
        return nullptr; // UnorderedSet 不支持 key 访问
    }

    uint32_t getElementTypeIndex() const override { return ya::type_index_v<T>; }

    std::unique_ptr<ContainerIterator> createIterator(void *containerPtr) override
    {
        return std::make_unique<UnorderedSetIterator>(static_cast<ContainerType *>(containerPtr));
    }

    void addElement(void *containerPtr, void *elementPtr) override
    {
        auto *set = static_cast<ContainerType *>(containerPtr);
        if (elementPtr) {
            set->insert(*static_cast<T *>(elementPtr));
        }
    }

    void removeByKey(void *containerPtr, void *keyPtr) override
    {
        if (!keyPtr) return;

        auto    *set = static_cast<ContainerType *>(containerPtr);
        const T &key = *static_cast<T *>(keyPtr);
        set->erase(key);
    }
};

// ============================================================================
// MARK: 容器属性工厂
// ============================================================================

template <typename T>
std::unique_ptr<IContainerProperty> createContainerProperty()
{
    if constexpr (is_container_v<T>) {
        using Traits = ContainerTraits<T>;

        if constexpr (Traits::Type == EContainer::Vector) {
            using ElementType = typename Traits::ElementType;
            return std::make_unique<VectorProperty<ElementType>>();
        }
        else if constexpr (Traits::Type == EContainer::Map) {
            using KeyType   = typename Traits::KeyType;
            using ValueType = typename Traits::ValueType;
            return std::make_unique<MapProperty<KeyType, ValueType>>();
        }
        else if constexpr (Traits::Type == EContainer::Set) {
            using ElementType = typename Traits::ElementType;
            return std::make_unique<SetProperty<ElementType>>();
        }
        else if constexpr (Traits::Type == EContainer::UnorderedMap) {
            using KeyType   = typename Traits::KeyType;
            using ValueType = typename Traits::ValueType;
            return std::make_unique<UnorderedMapProperty<KeyType, ValueType>>();
        }
        else if constexpr (Traits::Type == EContainer::UnorderedSet) {
            using ElementType = typename Traits::ElementType;
            return std::make_unique<UnorderedSetProperty<ElementType>>();
        }
    }

    return nullptr;
}

} // namespace ya::reflection
