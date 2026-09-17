#pragma once



#define VMA_DEBUG_LOG
#define VMA_RECORD_ALLOCATION_STACK_TRACES 1

// #define VMA_LEAK_LOG_FORMAT(format, ...) \
//     do {                                 \
//         printf((format), __VA_ARGS__);   \
//         printf("\n");                    \
//     } while (false);


#include "Core/Log.h"
#define VMA_LEAK_LOG_FORMAT(format, ...)                           \
    do {                                                           \
        int len = snprintf(nullptr, 0, format, __VA_ARGS__);       \
        if (len > 0) {                                             \
            std::vector<char> buf(len + 1);                        \
            snprintf(buf.data(), buf.size(), format, __VA_ARGS__); \
            YA_CORE_WARN("VMA Leak: {}", buf.data());              \
        }                                                          \
    } while (false)


#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
