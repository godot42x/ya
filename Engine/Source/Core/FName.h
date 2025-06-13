
#pragma once

#include <map>
#include <stdint.h>
#include <string>
#include <cassert>


#ifndef T_INTEGER
    #define T_INTEGER uint32_t
#endif


class NameRegistry
{
    static NameRegistry *_instance;

    std::map<std::string, T_INTEGER> _str2Index;
    T_INTEGER                        _nextIndex = 1; // 0 is reserved for empty name

  public:

    static void          init();
    static NameRegistry &instance() { return *_instance; }

    T_INTEGER getIndex(const std::string &name)
    {
        auto it = _str2Index.find(name);
        if (it != _str2Index.end())
        {
            return it->second;
        }
        else
        {
            // If name not found, add it
            T_INTEGER index  = _nextIndex++;
            _str2Index[name] = index;
            return index;
        }
    }
};

struct FName
{

    T_INTEGER   index;
    std::string data;

    FName() : index(0), data("") {}
    FName(const std::string &name)
        : index(NameRegistry::instance().getIndex(name)), data(name)
    {
    }

    operator T_INTEGER() const { return index; }
    operator const std::string &() const { return data; }
    bool operator==(const FName &other) const { return index == other.index; }
};


#ifndef ENABLE_TEST
    #define ENABLE_TEST 0
#endif

#if ENABLE_TEST
// Tests will be added later when test framework is built
#endif