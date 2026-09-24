
#include "RHI/Render.h"
#include "RHI/Backend/Vulkan/VulkanRender.h"

namespace ya
{

SurfaceId IRender::createSurfaceContext(INativeWindow& window, const SwapchainCreateInfo& swapchainCI)
{
    (void)window;
    (void)swapchainCI;
    // A backend that cannot register surfaces reports that as an invalid id
    // rather than by returning a half-built context the caller then owns.
    return {};
}

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
