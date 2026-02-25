#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#include "Core/Base.h"
#include "Core/FName.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/Pipeline.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include "Render/Shader.h"



// Texture management (shared across all pipelines)


namespace ya
{
struct ICommandBuffer;
struct VulkanRender;
struct VulkanRenderPass;

struct VulkanPipelineLayout : public IPipelineLayout
{
    std::string        _label;
    VulkanRender      *_render         = nullptr;
    ::VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    // std::vector<::VkDescriptorSetLayout> _descriptorSetLayouts = {};

    // ya::PipelineDesc _ci;

    VulkanPipelineLayout(VulkanRender *render, std::string label = std::string("None"))
        : _label(std::move(label)), _render(render) {}

    ~VulkanPipelineLayout() override { cleanup(); }

    VulkanPipelineLayout(const VulkanPipelineLayout &)            = delete;
    VulkanPipelineLayout &operator=(const VulkanPipelineLayout &) = delete;
    VulkanPipelineLayout(VulkanPipelineLayout &&)                 = default;
    VulkanPipelineLayout &operator=(VulkanPipelineLayout &&)      = default;

    void create(const std::vector<PushConstantRange>             pushConstants,
                const std::vector<stdptr<IDescriptorSetLayout>> &layouts);

    // IPipelineLayout interface
    void              *getHandle() const override { return (void *)(uintptr_t)_pipelineLayout; }
    const std::string &getLabel() const override { return _label; }

    ::VkPipelineLayout getVkHandle() const { return _pipelineLayout; }

    void cleanup();

    // ya::PipelineDesc getCI() const { return _ci; }
};



struct VulkanPipeline : public ya::IGraphicsPipeline
{
  public:
    FName _name;

    VkPipeline       _pipeline           = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool     = VK_NULL_HANDLE;

  private:
    ya::GraphicsPipelineCreateInfo _ci;

    VulkanRender         *_render         = nullptr;
    VulkanPipelineLayout *_pipelineLayout = nullptr;

  public:
    VulkanPipeline(VulkanRender *render)
    {
        _render = render;
        queryPhysicalDeviceLimits(); // maxTextureSlots
    }

    ~VulkanPipeline() override { cleanup(); }

    VulkanPipeline(const VulkanPipeline &)            = delete;
    VulkanPipeline &operator=(const VulkanPipeline &) = delete;
    VulkanPipeline(VulkanPipeline &&)                 = default;
    VulkanPipeline &operator=(VulkanPipeline &&)      = default;

    void cleanup();

    // IGraphicsPipeline interface
    bool recreate(const GraphicsPipelineCreateInfo &ci) override;
    void bind(CommandBufferHandle commandBuffer) override
    {
        YA_CORE_ASSERT(commandBuffer, "Invalid command buffer handle");
        vkCmdBindPipeline(commandBuffer.as<VkCommandBuffer>(),
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          _pipeline);
    }
    void              *getHandle() const override { return (void *)(uintptr_t)_pipeline; }
    const std::string &getName() const override
    {
        static std::string name_cache;
        name_cache = std::string(_name._data);
        return name_cache;
    }

    // Vulkan-specific methods
    void bindVk(VkCommandBuffer commandBuffer)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    }
    void bindVk(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint)
    {
        vkCmdBindPipeline(commandBuffer, bindPoint, _pipeline);
    }

    // 修改描述后仅标记 dirty，下一帧 beginFrame 重建 pipeline
    void updateDesc(GraphicsPipelineCreateInfo ci) override;
    void beginFrame() override;
    void renderGUI() override;

    void setSampleCount(ESampleCount::T sampleCount) override;
    ESampleCount::T getSampleCount() const override { return _ci.multisampleState.sampleCount; }
    void setCullMode(ECullMode::T cullMode) override;
    ECullMode::T getCullMode() const override { return _ci.rasterizationState.cullMode; }
    void setPolygonMode(EPolygonMode::T polygonMode) override;
    EPolygonMode::T getPolygonMode() const override { return _ci.rasterizationState.polygonMode; }

    ::VkPipeline getVkHandle() const { return _pipeline; }

  private:
    // Pipeline creation helpers
    VkShaderModule createShaderModule(const std::vector<uint32_t> &spv_binary);
    void           createPipelineInternal();

    void queryPhysicalDeviceLimits();
};

} // namespace ya