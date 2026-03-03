#pragma once

// Lightweight warning suppression macros — no other includes.

#if _WIN32
    #define YA_PUSH_WARNING_DISABLE(x) __pragma(warning(push)) __pragma(warning(disable : x))
    #define YA_POP_WARNING() __pragma(warning(pop))
#else
    #define YA_PUSH_WARNING_DISABLE(x)
    #define YA_POP_WARNING()
#endif

#define YA_DISABLE_PADDED_STRUCT_WARNING_BEGIN() YA_PUSH_WARNING_DISABLE(4324)
#define YA_DISABLE_PADDED_STRUCT_WARNING_END() YA_POP_WARNING()
#define YA_DISABLE_UNREACHABLE_CODE_WARNING_BEGIN() YA_PUSH_WARNING_DISABLE(768)
#define YA_DISABLE_UNREACHABLE_CODE_WARNING_END() YA_POP_WARNING()
