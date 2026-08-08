
#include "Foundation/RHI/Core/Swapchain.h"
#include "Foundation/RHI/Backend/Vulkan/VulkanSwapChain.h"

namespace ya
{

stdptr<ISwapchain> ISwapchain::create(IRender *render, const SwapchainCreateInfo &createInfo)
{
    (void)createInfo;
    YA_CORE_ASSERT(render, "ISwapchain::create requires render backend");
    auto api = render->getAPI();
    switch (api) {
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
        break;
    case ERenderAPI::Vulkan:
    {
        // VulkanSwapChain already exists, just need to wrap it
        // This would require refactoring VulkanSwapChain to inherit from ISwapchain
        // For now, return nullptr and handle separately
        return nullptr;
    }
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        break;
    }
    UNREACHABLE();
    return nullptr;
}

} // namespace ya
