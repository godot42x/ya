#pragma once

#include "Core/Common/Warnings.h"

#include "Common/FWD-std.h"
#include "Common/FWD.h"


#if _WIN32
    #define PLATFORM_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PLATFORM_BREAK() __builtin_trap()
#else
    #define PLATFORM_BREAK()
#endif


#if defined(_WIN32)
    #if BUILD_LIBRARY
        #if BUILD_SHARED_YA
            #define ENGINE_API __declspec(dllexport)
        #else
            #define ENGINE_API __declspec(dllimport)
        #endif
    #else
        #define ENGINE_API
    #endif
#else
    #define ENGINE_API
#endif

#if _WIN32
    #define FUNCTION_SIG __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
    #define FUNCTION_SIG __PRETTY_FUNCTION__
#else
    #define FUNCTION_SIG
#endif


#define USE_VULKAN 1
#define USE_STB_IMG 1
#define USE_SDL 1
// NOTE: FORWARD macro removed — pipeline selection is now runtime via App::EShadingModel



struct DefaultAllocator
{
    template <typename T>
    static T* allocate(std::size_t size)
    {
        return static_cast<T*>(malloc(size));
    }

    template <typename T>
    static void deallocate(T* ptr)
    {
        free(ptr);
    }
};

inline static DefaultAllocator defaultAllocator;

#define CASE_ENUM_TO_STR(x) \
    case x:                 \
        return #x;


// Portable strncpy_s (MSVC Annex K).
// On Windows the CRT provides it; on other platforms we provide an inline implementation.
// _TRUNCATE means "copy at most destSize-1 bytes and always null-terminate".
#include <cstring>
#if !defined(_WIN32)
    #ifndef _TRUNCATE
        #define _TRUNCATE ((size_t)-1)
    #endif
    inline int strncpy_s(char *dest, size_t destSize, const char *src, size_t count)
    {
        if (!dest || destSize == 0) return 22; // EINVAL
        if (!src) { dest[0] = '\0'; return 22; }
        size_t copyCount = (count == _TRUNCATE) ? destSize - 1 : count;
        if (copyCount >= destSize) copyCount = destSize - 1;
        std::memcpy(dest, src, copyCount);
        dest[copyCount] = '\0';
        return 0;
    }
    template <size_t N>
    inline int strncpy_s(char (&dest)[N], const char *src, size_t count)
    {
        return strncpy_s(dest, N, src, count);
    }
#endif