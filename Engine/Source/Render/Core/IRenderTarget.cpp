#include "IRenderTarget.h"
#include "Runtime/App/App.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanRenderTarget.h"
#endif

namespace ya
{


std::shared_ptr<IRenderTarget> createRenderTarget(const RenderTargetCreateInfo &ci)
{
    auto* render = App::get()->getRender();
    YA_CORE_ASSERT(render, "Render is null when creating render target");
    auto api = render->getAPI();
    switch (api) {
    case ERenderAPI::Vulkan:
    {
        auto ret = makeShared<VulkanRenderTarget>();
        if (!ret->init(ci)) {
            return nullptr;
        }
        return ret;
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
