
#pragma once

#include <cassert>
#include <format>
#include <map>
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
        index_t               index;
        std::atomic<uint32_t> refCount;
        std::string           data;
    };

  private:
    std::map<std::string, Elem> _str2Elem;
    std::map<index_t, Elem *>   _index2Elem;
    index_t                     _nextIndex = 1; // 0 is reserved for empty name

  public:

    static void          init();
    static NameRegistry &get() { return *_instance; }


    const Elem *indexing(const std::string &name)
    {
        if (auto it = _str2Elem.find(name); it != _str2Elem.end())
        {
            ++it->second.refCount;
            return &it->second;
        }

        // If name not found, add it
        index_t index = ++_nextIndex;

        auto it = _str2Elem.emplace(
            name,
            Elem{
                .index    = index,
                .refCount = std::atomic<uint32_t>(1),
                .data     = name,
            });

        _index2Elem[index] = &it.first->second;
        return &it.first->second;
    }

  protected:
    const Elem *indexing(index_t index)
    {
        if (auto it = _index2Elem.find(index); it != _index2Elem.end())
        {
            ++it->second->refCount;
            return it->second;
        }
        return nullptr;
    }

    void onRemove(const FName &name);
};

struct FName
{
    index_t          _index;
    std::string_view _data;

    FName() : _index(0), _data("") {}
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
    FName(FName &&other)
    {
        // do nothing, but access a moved FName is undefined behavior
        // maybe it already destruct in name registry, or also could be used elsewhere
        (void)other;
    }
    ~FName()
    {
        clear();
    }

    void clear()
    {
        NameRegistry::get().onRemove(*this);
    }
    std::string toString() { return std::string(_data); }

    operator index_t() const { return _index; }
    operator std::string_view() const { return _data; }
    operator const char *() const { return _data.data(); }

    bool operator==(const FName &other) const { return _index == other._index; }
};

namespace std
{
template <>
struct formatter<FName> : formatter<std::string>
{
    auto format(const FName &name, std::format_context &ctx) const
    {
        return std::format_to_n(ctx.out(),
                                static_cast<long long>(name._data.size()),
                                "{}",
                                name._data);
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

    if (auto it = _index2Elem.find(name._index); it != _index2Elem.end())
    {
        // atomic
        auto refCount = --it->second->refCount;
        if (refCount == 0) {
            _index2Elem.erase(it);
            _str2Elem.erase(std::string(name._data));
        }
    }
}

#ifndef ENABLE_TEST
    #define ENABLE_TEST 0
#endif

#if ENABLE_TEST
// Tests will be added later when test framework is built
#endif