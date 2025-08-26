
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
    static NameRegistry *_instance;

    struct Elem
    {
        index_t     index;
        uint32_t    refCount;
        std::string data;
    };

    std::map<std::string, Elem> _str2Index;
    index_t                     _nextIndex = 1; // 0 is reserved for empty name

  public:

    static void          init();
    static NameRegistry &get() { return *_instance; }


    const Elem *newIndex(const std::string &name)
    {
        auto it = _str2Index.find(name);
        if (it != _str2Index.end())
        {
            it->second.refCount++;
            return &it->second;
        }

        // If name not found, add it
        index_t     index = _nextIndex++;
        const auto &ret   = _str2Index.insert({
            name,
            Elem{
                  .index    = index,
                  .refCount = 1,
                  .data     = name},
        });
        return &ret.first->second;
    }
    bool ref(const std::string &name)
    {
        auto it = _str2Index.find(name);
        if (it != _str2Index.end())
        {
            it->second.refCount++;
            return true;
        }
        return false;
    }

    void deref(const std::string &name)
    {
        auto it = _str2Index.find(name);
        if (it != _str2Index.end())
        {
            --it->second.refCount;
            if (it->second.refCount == 0)
            {
                _str2Index.erase(it);
            }
        }
    }
};

struct FName
{
    index_t          index;
    std::string_view data;
    std::shared_ptr<int> a;

    FName() : index(0), data("") {}
    FName(const FName &)            = default;
    FName(FName &&)                 = delete;
    FName &operator=(const FName &) = default;
    FName &operator=(FName &&)      = delete;
    FName(const std::string &name)
    {
        auto *elem = NameRegistry::get().newIndex(name);
        index      = elem->index;
        data       = elem->data;
    }
    FName(const char *name)
        : FName(std::string(name))
    {
    }
    ~FName()
    {
        clear();
    }

    void clear()
    {
        NameRegistry::get().deref(data);
        index = 0;
    }
    std::string toString() { return std::string(data); }

    operator index_t() const { return index; }
    operator std::string_view() const { return data; }
    operator const char *() const { return data.data(); }

    bool operator==(const FName &other) const { return index == other.index; }
};

namespace std
{
template <>
struct formatter<FName> : formatter<std::string>
{
    auto format(const FName &name, std::format_context &ctx) const
    {
        return std::format_to_n(ctx.out(),
                                static_cast<long long>(name.data.size()),
                                "{}",
                                name.data);
    }
};

template <>
struct hash<FName>
{
    std::size_t operator()(const FName &name) const
    {
        // Use a simple hash function for FName
        return name.index;
    }
};

} // namespace std

#ifndef ENABLE_TEST
    #define ENABLE_TEST 0
#endif

#if ENABLE_TEST
// Tests will be added later when test framework is built
#endif