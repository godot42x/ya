#pragma once
#include "Core/Base.h"

#include "ECS/System.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
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

    struct FrameUBO
    {
        glm::mat4 projection{1.f};
        glm::mat4 view{1.f};
        alignas(8) glm::ivec2 resolution;
        alignas(4) uint32_t frameIndex = 0;
        alignas(4) float time;
    };
    struct ModelPushConstant
    {
        glm::mat4 modelMat;
    };

    struct alignas(16) LightUBO
    {
        glm::vec3 lightDirection   = glm::vec3(-0.5f, -1.0f, -0.3f);
        float     lightIntensity   = 1.0f;
        glm::vec3 lightColor       = glm::vec3(1.0f);
        float     ambientIntensity = 0.1f;
        glm::vec3 ambientColor     = glm::vec3(0.1f);
    };


    GraphicsPipelineCreateInfo _pipelineDesc;

    std::shared_ptr<IDescriptorSetLayout> _materialFrameDSL; // set 0
    // std::shared_ptr<IDescriptorSetLayout> _materialDSL;       // set 1
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL; // set 2

    std::shared_ptr<IPipelineLayout>   _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline> _pipeline;

    // set 0, contains the frame UBO and lighting UBO
    std::shared_ptr<IDescriptorPool> _frameDSP;
    DescriptorSetHandle              _frameDS;
    std::shared_ptr<IBuffer>         _frameUBO;
    std::shared_ptr<IBuffer>         _lightUBO;



    // material ubo's, dynamically extend
    uint32_t                         _lastMaterialDSCount = 0;
    std::shared_ptr<IDescriptorPool> _materialDSP;

    // object ubo
    std::vector<std::shared_ptr<IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>      _materialParamDSs; // each material instance
    // std::vector<DescriptorSetHandle>      _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override;
    void onUpdate(float deltaTime) override {}
    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }



  private:
    // void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(IRenderTarget *rt);
    void updateMaterialParamDS(DescriptorSetHandle ds, LitMaterial *material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, LitMaterial *material);
    void recreateMaterialDescPool(uint32_t _materialCount);
};

} // namespace ya