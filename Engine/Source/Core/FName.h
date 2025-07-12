
#pragma once

#include <cassert>
#include <format>
#include <map>
#include <stdint.h>
#include <string>



using index_t = uint32_t;


class NameRegistry
{
    static NameRegistry *_instance;

    std::map<std::string, index_t> _str2Index;
    index_t                        _nextIndex = 1; // 0 is reserved for empty name

  public:

    static void          init();
    static NameRegistry &instance() { return *_instance; }

    index_t getIndex(const std::string &name)
    {
        auto it = _str2Index.find(name);
        if (it != _str2Index.end())
        {
            return it->second;
        }
        else
        {
            // If name not found, add it
            index_t index    = _nextIndex++;
            _str2Index[name] = index;
            return index;
        }
    }
};

struct FName
{

    index_t     index;
    std::string data;

    FName() : index(0), data("") {}
    FName(const std::string &name)
        : index(NameRegistry::instance().getIndex(name)), data(name)
    {
    }
    FName(const char *name)
        : index(NameRegistry::instance().getIndex(std::string(name))), data(name)
    {
    }

    void clear()
    {
        index = 0;
        data.clear();
    }
    operator index_t() const { return index; }
    operator const std::string &() const { return data; }
    operator const char *() const { return data.c_str(); }
    bool operator==(const FName &other) const { return index == other.index; }
};


namespace std
{
// Add formatter specialization for spirv_cross::SPIRType
template <>
struct formatter<FName> : formatter<std::string>
{
    auto format(const FName &type, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "%s", type.data.c_str());
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