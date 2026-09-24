#include "RHI/Core/RenderPass.h"

#include "RHI/Backend/Vulkan/VulkanRender.h"
#include "RHI/Render.h"


namespace ya
{


std::shared_ptr<IRenderPass> IRenderPass::create(IRender *render, ISwapchain &swapchain, const RenderPassCreateInfo &ci)
{
    if (!render)
        return nullptr;

    switch (render->getAPI())
    {
    case ERenderAPI::Vulkan:
    {
        auto ret = makeShared<VulkanRenderPass>(render->as<VulkanRender>(), &swapchain);
        ret->recreate(ci);
        return ret;
    }
    default:
        return nullptr;
    }
}

} // namespace ya
