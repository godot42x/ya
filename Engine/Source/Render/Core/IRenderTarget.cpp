#include "IRenderTarget.h"
#include "Core/App/App.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanRenderTarget.h"
#endif

namespace ya
{

// Factory functions - create platform-specific implementations
std::shared_ptr<IRenderTarget> createRenderTarget(IRenderPass *renderPass)
{
#if USE_VULKAN
    return makeShared<VulkanRenderTarget>(renderPass);
#else
    #error "Platform not supported"
#endif
}

std::shared_ptr<IRenderTarget> createRenderTarget(RenderTargetCreateInfo ci)
{

    auto api = App::get()->currentRenderAPI;
    switch (api) {
    case ERenderAPI::Vulkan:
    {
        stdptr<IRenderTarget> rt;
        if (ci.bSwapChainTarget) {
            rt = makeShared<VulkanRenderTarget>(ci.renderPass);
        }
        else {
            rt = makeShared<VulkanRenderTarget>(ci.renderPass, ci.frameBufferCount, ci.extent);
        }
        rt->label = ci.label;
        return rt;

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
