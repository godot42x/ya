
#pragma once

#if _WIN32
    #define __FUNCTION_SIG __FUNCSIG__
#elif defined(__linux__) || defined(__APPLE__)
    #define __FUNCTION_SIG __PRETTY_FUNCTION__
#else
    #error Need your implementation
#endif

// 定义字符串转换工具宏
#define TO_STRING_IMPL(x) #x
#define TO_STRING(x) TO_STRING_IMPL(x)

// 根据不同编译器生成编译警告
#if defined(__GNUC__) || defined(__clang__)
    #define COMPILER_WARNING(msg) _Pragma(TO_STRING(message(msg)))
#elif defined(_MSC_VER)
    #define COMPILER_WARNING(msg) __pragma(message(msg))
#else
    #define COMPILER_WARNING(msg)
#endif

// 最终的 UNIMPLEMENTED 宏
#define UNIMPLEMENTED()                                                                  \
    do {                                                                                 \
        COMPILER_WARNING("WARNING: UNIMPLEMENTED at " __FILE__ ":" TO_STRING(__LINE__)); \
        fprintf(stderr, "TODO unimplemented: %s:%d\n", __FILE__, __LINE__);              \
        assert(0);                                                                       \
    } while (0)

#define UNREACHABLE()                                                \
    do {                                                             \
        fprintf(stderr, "unreachable: %s:%d\n", __FILE__, __LINE__); \
        assert(0);                                                   \
    } while (0)

#define TODO(msg) COMPILER_WARNING("@TODO " #msg "(" __FILE__ ":" TO_STRING(__LINE__) ")")
