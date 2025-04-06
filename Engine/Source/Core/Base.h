#if _WIN32
    #define PLATFORM_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PLATFORM_BREAK() __builtin_trap()
#else
    #define PLATFORM_BREAK()
#endif


#if defined(_WIN32)
    #if BUILD_SHARED_NEON
        #define NEON_API __declspec(dllexport)
    #else
        #define NEON_API __declspec(dllimport)
    #endif
#else
    #define NEON_API
#endif
