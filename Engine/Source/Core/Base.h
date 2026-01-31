#pragma once
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



struct DefaultAllocator
{
    template <typename T>
    static T *allocate(std::size_t size)
    {
        return static_cast<T *>(malloc(size));
    }

    template <typename T>
    static void deallocate(T *ptr)
    {
        free(ptr);
    }
};

inline static DefaultAllocator defaultAllocator;

#define CASE_ENUM_TO_STR(x) \
    case x:                 \
        return #x;


