#pragma once

#include <cassert>
#include <format>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <stdint.h>
#include <string>

namespace ya
{


using index_t = uint32_t;

struct FName;

class NameRegistry
{
  public:
    friend struct FName;

    struct Elem
    {
        index_t     index = 0;
        std::string data;
    };

  private:
    std::map<std::string, Elem> _str2Elem;
    index_t                     _nextIndex = 1; // 0 is reserved for empty name
    mutable std::shared_mutex   _mutex;         // 读写锁：读操作共享，写操作独占

  public:

    static NameRegistry &get();
    static NameRegistry *getP() { return &get(); }


    // register the string
    const Elem *indexing(const std::string &name)
    {
        // 第一次检查：使用共享锁（读锁）- 高并发场景下性能更好
        {
            std::shared_lock<std::shared_mutex> lock(_mutex);
            if (auto it = _str2Elem.find(name); it != _str2Elem.end())
            {
                return &it->second;
            }
        }

        // 第二次检查：使用独占锁（写锁）- 只有创建新字符串时才需要
        std::unique_lock<std::shared_mutex> lock(_mutex);

        // 双重检查：可能在等待锁期间其他线程已经创建了
        if (auto it = _str2Elem.find(name); it != _str2Elem.end())
        {
            return &it->second;
        }

        // If name not found, add it
        index_t index = ++_nextIndex;

        auto it = _str2Elem.insert({name, Elem(index, name)});
        return &it.first->second;
    }

  protected:

    void remove(const FName &name);
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
    ~FName() {}


    std::string toString() const { return std::string(_data); }
    const char *c_str() const { return _data.data(); }

    bool isValid() const noexcept { return _index != 0; }
    bool isEmpty() const noexcept { return _index == 0; }

    operator index_t() const noexcept { return _index; }
    operator std::string_view() const noexcept { return isValid() ? _data : "None"; }
    operator const char *() const noexcept { return isValid() ? _data.data() : "None"; }
    operator std::string() const noexcept { return isValid() ? std::string(_data) : "None"; }

    bool operator==(const FName &other) const noexcept { return _index == other._index; }
    bool operator!=(const FName &other) const noexcept { return _index != other._index; }
    bool operator<(const FName &other) const noexcept { return _index < other._index; }

    index_t identity() const { return _index; }
};

inline void NameRegistry::remove(const FName &name)
{
    if (name._index == 0) return; // 空 FName，不需要处理

    // 只需要修改 refCount（atomic），使用共享锁即可
    std::shared_lock<std::shared_mutex> lock(_mutex);
    _str2Elem.erase(name.toString());
}

namespace literals
{

inline FName operator""_name(const char *str, size_t len)
{
    return FName(std::string_view(str, len));
}
}; // namespace literals


} // namespace ya

namespace std
{
template <>
struct formatter<ya::FName> : formatter<std::string>
{
    auto format(const ya::FName &name, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "{}", name._data);
    }
};

template <>
struct hash<ya::FName>
{
    std::size_t operator()(const ya::FName &name) const
    {
        // Use a simple hash function for FName
        return name._index;
    }
};

} // namespace std
