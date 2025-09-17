#pragma once
#include "Core/Base.h"

#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/RenderDefines.h"
#include "vulkan/vulkan_core.h"


struct VulkanPipelineLayout;
struct VulkanPipeline;
struct VulkanDescriptorSetLayout;
struct VulkanDescriptorPool;
struct VulkanBuffer;


namespace ya
{

struct UnlitMaterial;

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct UnlitMaterialSystem : public IMaterialSystem
{
    GraphicsPipelineCreateInfo            _pipelineDesc;
    std::shared_ptr<VulkanPipeline>       _pipeline;
    std::shared_ptr<VulkanPipelineLayout> _pipelineLayout;


    std::shared_ptr<VulkanDescriptorSetLayout> _materialFrameUboDSL; // set0
    std::shared_ptr<VulkanDescriptorSetLayout> _materialParamDSL;    // set 1
    std::shared_ptr<VulkanDescriptorSetLayout> _materialResourceDSL; // set 2

    // frame ubo's  constantly
    std::shared_ptr<VulkanDescriptorPool> _frameDSP;
    VkDescriptorSet                       _frameDS;
    std::shared_ptr<VulkanBuffer>         _frameUBO;

    // material ubo's, dynamically extend
    uint32_t                                   _lastMaterialDSCount = 0;
    std::shared_ptr<VulkanDescriptorPool>      _materialDSP;
    std::vector<std::shared_ptr<VulkanBuffer>> _materialParamsUBOs;
    std::vector<VkDescriptorSet>               _materialParamDSs;    // each material instance
    std::vector<VkDescriptorSet>               _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    void onInit(VulkanRenderPass *renderPass) override;
    void onDestroy() override {}
    void onUpdate(float deltaTime) override {}
    void onRender(void *cmdBuf, RenderTarget *rt) override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(RenderTarget *rt);
    void updateMaterialParamDS(VkDescriptorSet ds, UnlitMaterial *material);
    void updateMaterialResourceDS(VkDescriptorSet ds, UnlitMaterial *material);
};

} // namespace ya