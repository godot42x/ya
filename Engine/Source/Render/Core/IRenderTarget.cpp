#include "IRenderTarget.h"
#include "Core/App/App.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanRenderTarget.h"
#endif

namespace ya
{


std::shared_ptr<IRenderTarget> createRenderTarget(RenderTargetCreateInfo ci)
{

    auto api = App::get()->currentRenderAPI;
    switch (api) {
    case ERenderAPI::Vulkan:
    {
        return makeShared<VulkanRenderTarget>(ci);
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
