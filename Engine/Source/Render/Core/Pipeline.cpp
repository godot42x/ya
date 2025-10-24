#include "Pipeline.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Render/Render.h"
#include "RenderPass.h"


namespace ya
{

std::shared_ptr<IPipelineLayout> IPipelineLayout::create(
    IRender                                         *render,
    const std::string                               &label,
    const std::vector<PushConstantRange>            &pushConstants,
    const std::vector<stdptr<IDescriptorSetLayout>> &descriptorSetLayouts)
{
    if (!render)
        return nullptr;

    switch (render->getAPI())
    {
    case ERenderAPI::Vulkan:
    {
        try {
            auto vkRender  = render->as<VulkanRender>();
            auto pipLayout = makeShared<VulkanPipelineLayout>(vkRender, label);
            pipLayout->create(pushConstants, descriptorSetLayouts);
            return pipLayout;
        }
        catch (...) {
            YA_CORE_ERROR("Failed to create VulkanPipelineLayout");
            return nullptr;
        }
    }
    default:
        return nullptr;
    }
}

std::shared_ptr<IGraphicsPipeline> IGraphicsPipeline::create(
    IRender         *render,
    IRenderPass     *renderPass,
    IPipelineLayout *pipelineLayout)
{
    if (!render || !renderPass || !pipelineLayout)
        return nullptr;

    switch (render->getAPI())
    {
    case ERenderAPI::Vulkan:
    {
        // Create Vulkan pipeline
        // Note: actual implementation will need to include VulkanPipeline.h in cpp file
        // For now, return nullptr - this will be implemented when VulkanPipeline
        // inherits from IGraphicsPipeline
        auto vkRender     = render->as<VulkanRender>();
        auto vkRenderPass = renderPass->as<VulkanRenderPass>();
        auto vkPipLayout  = pipelineLayout->as<VulkanPipelineLayout>();
        auto ret          = makeShared<VulkanPipeline>(vkRender, vkRenderPass, vkPipLayout);
        YA_CORE_ASSERT(ret != nullptr, "Failed to create VulkanPipeline");
        return ret;
    }
    default:
        return nullptr;
    }
}

} // namespace ya
