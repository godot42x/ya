#pragma once
#include "Core/Base.h"

#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/RenderDefines.h"


struct VulkanPipelineLayout;
struct VulkanPipeline;
struct VulkanBuffer;


namespace ya
{

struct LitMaterial;

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct LitMaterialSystem : public IMaterialSystem
{
    GraphicsPipelineCreateInfo            _pipelineDesc;
    std::shared_ptr<VulkanPipeline>       _pipeline;
    std::shared_ptr<VulkanPipelineLayout> _pipelineLayout;


    std::shared_ptr<ya::IDescriptorSetLayout> _materialFrameUboDSL; // set0
    std::shared_ptr<ya::IDescriptorSetLayout> _materialParamDSL;    // set 1
    std::shared_ptr<ya::IDescriptorSetLayout> _materialResourceDSL; // set 2

    // frame ubo's  constantly
    std::shared_ptr<ya::IDescriptorPool> _frameDSP;
    DescriptorSetHandle                  _frameDS;
    std::shared_ptr<VulkanBuffer>        _frameUBO;

    // material ubo's, dynamically extend
    uint32_t                                   _lastMaterialDSCount = 0;
    std::shared_ptr<IDescriptorPool>           _materialDSP;
    std::vector<std::shared_ptr<VulkanBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>           _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>           _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override {}
    void onUpdate(float deltaTime) override {}
    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(IRenderTarget *rt);
    void updateMaterialParamDS(DescriptorSetHandle ds, LitMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, LitMaterial *material);
};

} // namespace ya