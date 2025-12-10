#pragma once

#include <cassert>
#include <format>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <stdint.h>
#include <string>



using index_t = uint32_t;

struct FName;

class NameRegistry
{
  public:
    friend struct FName;

    static NameRegistry *_instance;

    struct Elem
    {
        index_t               index = 0;
        std::atomic<uint32_t> refCount{0};
        std::string           data;

        // Make Elem movable but not copyable
        Elem() = default;
        Elem(index_t idx, uint32_t count, std::string str)
            : index(idx), refCount(count), data(std::move(str)) {}

        ~Elem() = default;

        Elem(const Elem &)            = delete;
        Elem &operator=(const Elem &) = delete;

        Elem(Elem &&other) noexcept
            : index(other.index), refCount(other.refCount.load()), data(std::move(other.data)) {}

        Elem &operator=(Elem &&other) noexcept
        {
            if (this != &other) {
                index = other.index;
                refCount.store(other.refCount.load());
                data = std::move(other.data);
            }
            return *this;
        }
    };

  private:
    std::map<std::string, Elem> _str2Elem;
    std::map<index_t, Elem *>   _index2Elem;
    index_t                     _nextIndex = 1; // 0 is reserved for empty name
    mutable std::shared_mutex   _mutex;         // 读写锁：读操作共享，写操作独占

  public:

    static NameRegistry &get();
    static NameRegistry *getP() { return &get(); }


    const Elem *indexing(const std::string &name)
    {
        // 第一次检查：使用共享锁（读锁）- 高并发场景下性能更好
        {
            std::shared_lock<std::shared_mutex> lock(_mutex);
            if (auto it = _str2Elem.find(name); it != _str2Elem.end())
            {
                ++it->second.refCount;  // refCount 是 atomic，线程安全
                return &it->second;
            }
        }

        // 第二次检查：使用独占锁（写锁）- 只有创建新字符串时才需要
        std::unique_lock<std::shared_mutex> lock(_mutex);
        
        // 双重检查：可能在等待锁期间其他线程已经创建了
        if (auto it = _str2Elem.find(name); it != _str2Elem.end())
        {
            ++it->second.refCount;
            return &it->second;
        }

        // If name not found, add it
        index_t index = ++_nextIndex;

        auto it = _str2Elem.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(name),
                                    std::forward_as_tuple(index, 1, name));

        _index2Elem[index] = &it.first->second;
        return &it.first->second;
    }

  protected:
    const Elem *indexing(index_t index)
    {
        // 只读操作，使用共享锁
        std::shared_lock<std::shared_mutex> lock(_mutex);

        if (auto it = _index2Elem.find(index); it != _index2Elem.end())
        {
            ++it->second->refCount;  // atomic 操作
            return it->second;
        }
        return nullptr;
    }

    void onRemove(const FName &name);
};

struct FName
{
    index_t          _index = 0;
    std::string_view _data;

    FName() : _data("") {}
    FName(const std::string &name)
    {
        auto *elem = NameRegistry::get().indexing(name);
        _index     = elem->index;
        _data      = elem->data;
    }
    FName(const char *name) : FName(std::string(name)) {}
    FName(std::string_view name) : FName(std::string(name)) {}
    FName(const FName &other)
    {
        assert(nullptr != NameRegistry::get().indexing(other._index)); // add ref
        _index = other._index;
        _data  = other._data;
    }
    FName(FName &&other) noexcept : _index(other._index), _data(other._data)
    {
        // 移动构造需要增加引用计数，因为现在有两个 FName 指向同一个 registry entry
        // other 的析构函数会减少引用计数，所以需要先增加
        if (_index != 0) {
            NameRegistry::get().indexing(_index); // 增加引用计数
        }
        // Reset the moved-from object
        other._index = 0;
        other._data  = "";
    }

    FName &operator=(const FName &other)
    {
        if (this != &other) {
            clear();
            assert(nullptr != NameRegistry::get().indexing(other._index)); // add ref
            _index = other._index;
            _data  = other._data;
        }
        return *this;
    }

    FName &operator=(FName &&other) noexcept
    {
        if (this != &other) {
            clear();
            _index = other._index;
            _data  = other._data;
            // 移动赋值也需要增加引用计数
            if (_index != 0) {
                NameRegistry::get().indexing(_index); // 增加引用计数
            }
            other._index = 0;
            other._data  = "";
        }
        return *this;
    }

    ~FName()
    {
        clear();
    }

    void clear()
    {
        if (_index != 0) {
            NameRegistry::get().onRemove(*this);
            _index = 0;
            _data  = "";
        }
    }
    std::string toString() const { return std::string(_data); }
    const char *c_str() const { return _data.data(); }

    bool isValid() const noexcept { return _index != 0; }
    bool isEmpty() const noexcept { return _index == 0; }

    operator index_t() const noexcept { return _index; }
    operator std::string_view() const noexcept { return _data; }
    operator const char *() const noexcept { return _data.data(); }

    bool operator==(const FName &other) const noexcept { return _index == other._index; }
    bool operator!=(const FName &other) const noexcept { return _index != other._index; }
    bool operator<(const FName &other) const noexcept { return _index < other._index; }
};

namespace std
{
template <>
struct formatter<FName> : formatter<std::string>
{
    auto format(const FName &name, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "{}", name._data);
    }
};

template <>
struct hash<FName>
{
    std::size_t operator()(const FName &name) const
    {
        // Use a simple hash function for FName
        return name._index;
    }
};

} // namespace std

inline void NameRegistry::onRemove(const FName &name)
{
    if (name._index == 0) return; // 空 FName，不需要处理

    // 只需要修改 refCount（atomic），使用共享锁即可
    std::shared_lock<std::shared_mutex> lock(_mutex);

    if (auto it = _index2Elem.find(name._index); it != _index2Elem.end())
    {
        // 减少引用计数，但不删除 entry
        // 这确保相同字符串永远得到相同的 index（string interning 语义）
        // 这是 FName 的核心设计：字符串一旦注册，index 永久有效
        if (it->second->refCount > 0) {
            --it->second->refCount;
        }

        // 注意：不删除 entry，即使 refCount == 0
        // 优点：
        // 1. 相同字符串在程序生命周期内 index 始终一致
        // 2. 避免 hash map key 失效问题
        // 3. 内存开销可控（只是字符串表）
        // 4. 线程安全：不会有删除导致的迭代器失效
        // 缺点：
        // 1. 字符串表只增不减（可以通过显式清理接口解决）
    }
}

#ifndef ENABLE_TEST
    #define ENABLE_TEST 0
#endif

#if ENABLE_TEST
// Tests will be added later when test framework is built
#endif