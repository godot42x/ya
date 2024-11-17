#include "base.h"

void panic(const std::string &msg, int code)
{
    NE_ERROR(msg);
    PLATFORM_BREAK();
    std::exit(code);
}


namespace std
{
const char *to_string(ERenderAPI bit)
{
    switch (bit) {
    case ERenderAPI::VULKAN:
        return "Vulkan";
    case ERenderAPI::OPENGL:
        return "OpenGL";
    case ERenderAPI::D3D12:
        return "D3D12";
    case ERenderAPI::D3D11:
        return "D3D11";
    case ERenderAPI::METAL:
        return "Metal";
    }
    assert(false);
    return "Unknown";
}

} // namespace std