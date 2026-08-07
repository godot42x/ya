
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"

namespace ya
{

IRender *IRender::create(const RenderCreateInfo &ci)
{

    IRender *render = nullptr;
    switch (ci.renderAPI) {
    case ERenderAPI::Vulkan:
    {
        render = new VulkanRender();
    } break;
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        UNREACHABLE();
        break;
    }
    render->_renderAPI = ci.renderAPI;
    return render;
}

} // namespace ya
