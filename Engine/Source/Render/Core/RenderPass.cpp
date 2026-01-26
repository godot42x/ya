#include "RenderPass.h"

#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"


namespace ya
{


std::shared_ptr<IRenderPass> IRenderPass::create(IRender *render, const RenderPassCreateInfo &ci)
{
    if (!render)
        return nullptr;

    switch (render->getAPI())
    {
    case ERenderAPI::Vulkan:
    {
        auto ret = makeShared<VulkanRenderPass>(render->as<VulkanRender>());
        ret->recreate(ci);
        return ret;
    }
    default:
        return nullptr;
    }
}

} // namespace ya
