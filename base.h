#pragma once

#include <string>

#include "log.h"


#if defined(_WIN32)
    #define PLATFORM_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define PLATFORM_BREAK() __builtin_trap()
#else
    #define PLATFORM_BREAK()
#endif

void panic(const std::string &msg, int code = 1);


#define NE_ASSERT(expr, ...)             \
    if (!!!(expr)) {                     \
        panic(std::format(__VA_ARGS__)); \
    }


static enum class ERenderAPI {
    VULKAN = 0,
    OPENGL = 1,
    D3D12  = 2,
    D3D11  = 3,
    METAL  = 4
} RenderAPI = ERenderAPI::VULKAN;

struct Layer
{
    virtual void Init()     = 0;
    virtual void Uninit()   = 0;
    virtual void OnUpdate() = 0;
};


namespace std
{

const char *to_string(ERenderAPI bit);
} // namespace std