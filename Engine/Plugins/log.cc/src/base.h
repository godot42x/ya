#pragma once

#ifdef _WIN32
    #ifdef BUILD_SHARED_LOG_CC
        #define LOG_CC_API __declspec(dllexport)
    #else
        #define LOG_CC_API __declspec(dllimport)
    #endif
#else
    // On ELF / Mach-O, default visibility works out of the box for static libs
    // and is what clang uses by default for shared libs unless -fvisibility=hidden
    // is set. Expanding to `extern` (as we did before) turned
    // `struct LOG_CC_API debug { ... }` into `struct extern debug { ... }`,
    // which clang rejects; leave the attribute empty here and add
    // `extern` explicitly where variables actually need it.
    #define LOG_CC_API
#endif



#ifndef __top_level_namespace
    #define __top_level_namespace logcc
#endif

#define TOP_LEVEL_NAMESPACE_BEGIN   \
    namespace __top_level_namespace \
    {
#define TOP_LEVEL_NAMESPACE_END } // namespace __top_level_namespace



#include <iostream>
#include <source_location>

struct LOG_CC_API debug

{
    std::ostream &out = std::cout;

    debug(std::string_view sig = "-", std::source_location location = std::source_location::current())
    {
#if NDEBUG
        out << sig << ' ';
#else
        out << sig << ' ' << location.file_name() << ':' << location.line() << ' ';
#endif
    }

    debug &operator,(auto msg)
    {
        out << msg << ' ';
        return *this;
    }

    ~debug()
    {
        out << '\n';
    }
};