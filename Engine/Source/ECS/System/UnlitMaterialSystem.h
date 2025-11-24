#pragma once
#include "Core/Base.h"

#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/RenderDefines.h"


namespace ya
{

struct UnlitMaterial;


struct UnlitMaterialSystem : public IMaterialSystem
{


    struct FrameUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution;
        alignas(4) uint32_t frameIndex = 0;
        alignas(4) float time;
    };
    struct PushConstant
    {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        alignas(16) glm::mat3 normalMatrix{1.0f};
    };


    static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
    static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;

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
    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(IRenderTarget *rt);
    void updateMaterialParamDS(DescriptorSetHandle ds, UnlitMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial *material);
};

} // namespace ya