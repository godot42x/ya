#pragma once
#include "Core/Base.h"

#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/RenderDefines.h"


namespace ya
{

struct UnlitMaterial;

static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;



struct UnlitMaterialSystem : public IMaterialSystem
{
    GraphicsPipelineCreateInfo         _pipelineDesc;
    std::shared_ptr<IGraphicsPipeline> _pipelineOwner;
    IGraphicsPipeline                 *_pipeline       = nullptr;
    stdptr<IPipelineLayout>            _pipelineLayout = nullptr;


    std::shared_ptr<IDescriptorSetLayout> _materialFrameUboDSL; // set0
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL;    // set 1
    std::shared_ptr<IDescriptorSetLayout> _materialResourceDSL; // set 2

    // frame ubo's  constantly
    std::shared_ptr<ya::IDescriptorPool> _frameDSP;
    DescriptorSetHandle                  _frameDS;
    std::shared_ptr<ya::IBuffer>         _frameUBO;

    // material ubo's, dynamically extend
    uint32_t                                  _lastMaterialDSCount = 0;
    std::shared_ptr<ya::IDescriptorPool>      _materialDSP;
    std::vector<std::shared_ptr<ya::IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>          _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>          _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override {}
    void onUpdate(float deltaTime) override {}
    void onRender(ICommandBuffer *cmdBuf, RenderTarget *rt) override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(RenderTarget *rt);
    void updateMaterialParamDS(DescriptorSetHandle ds, UnlitMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial *material);
};

} // namespace ya