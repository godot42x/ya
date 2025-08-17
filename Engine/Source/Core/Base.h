#pragma once
#include "FWD.h"
#include "Trait.h"

#if _WIN32
    #define PLATFORM_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PLATFORM_BREAK() __builtin_trap()
#else
    #define PLATFORM_BREAK()
#endif


#if defined(_WIN32)
    #if BUILD_SHARED_ya
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
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

template <typename T>
using Ref = std::shared_ptr<T>;
template <typename T>
using Owned = std::unique_ptr<T>;
template <typename T>
using WeakRef = std::weak_ptr<T>;
template <typename T, typename... Args>

Ref<T> MakeRef(Args &&...args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}
template <typename T, typename... Args>
Owned<T> MakeOwned(Args &&...args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}



#define USE_VULKAN 1
#define USE_STB_IMG 1
#define USE_SDL 1

#include "Log.h"


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

static DefaultAllocator defaultAllocator;


#define CASE_ENUM_TO_STR(x) \
    case x:                 \
        return #x;