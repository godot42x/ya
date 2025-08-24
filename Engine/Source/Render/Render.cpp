
#include "Render.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{

IRender *IRender::create(const RenderCreateInfo &ci)
{
    switch (ci.renderAPI) {
    case ERenderAPI::Vulkan:
    {
        IRender *render = new VulkanRender();
        return render;
    } break;
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        UNREACHABLE();
        break;
    }
    return nullptr;
}

} // namespace ya