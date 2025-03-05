#include <format>
#include <iostream>

#if _WIN32
    #define PLATFORM_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PLATFORM_BREAK() __builtin_trap()
#else
    #define PLATFORM_BREAK()
#endif

namespace ELogLevel
{
enum
{
    Trace = 0,
    Info,
    Warn,
    Error,
};

}


template <typename... Args>
inline void Log(const char *fmt, Args... args)
{
    std::format(std::forward<const char *>(fmt), std::forward<Args>(args)...);
    std::cout << fmt << std::endl;
}

int a()
{
    Log("{}", "test");
}

#define NE_LOG(...)

#define NE_CORE_ASSERT(condition, ...)             \
    {                                              \
        if (!(condition)) {                        \
            NE_LOG(ELogLevel::Error, __VA_ARGS__); \
            PLATFORM_BREAK();                      \
        }                                          \
    }
