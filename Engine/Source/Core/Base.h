#pragma once
#include "FWD.h"

using stdpath  = std::filesystem::path;
using stdclock = std::chrono::steady_clock;
using namespace std::string_literals;
using namespace std::literals;


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

template <typename T>
using stdptr = std::shared_ptr<T>;

template <typename T, typename... Args>
std::shared_ptr<T> makeShared(Args &&...args)
    // requires requires(T, Args... args) { new T(std::forward<Args>(args)...); }
    requires std::is_constructible_v<T, Args...>
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
std::unique_ptr<T> makeUnique(Args &&...args)
    requires std::is_constructible_v<T, Args...>
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}


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

static DefaultAllocator defaultAllocator;


#define CASE_ENUM_TO_STR(x) \
    case x:                 \
        return #x;



namespace ya
{

template <typename T>
struct TypeIndex
{
    inline static uint32_t counter = 0;

    static uint32_t value()
    {
        static uint32_t index = counter++;
        return index;
    }
};

template <typename T>
inline static const auto type_index_v = TypeIndex<T>::value();

} // namespace ya