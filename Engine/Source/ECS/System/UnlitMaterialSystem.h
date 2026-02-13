#pragma once
#include "Core/Base.h"

#include "Core/System/System.h"
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

    GraphicsPipelineCreateInfo _pipelineDesc;
    // std::shared_ptr<IGraphicsPipeline> _pipelineOwner;
    // IGraphicsPipeline                 *_pipeline       = nullptr; // temp move to IMaterialSystem
    stdptr<IPipelineLayout> _pipelineLayout = nullptr;


    std::shared_ptr<IDescriptorSetLayout> _materialFrameUboDSL; // set0
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL;    // set 1
    std::shared_ptr<IDescriptorSetLayout> _materialResourceDSL; // set 2

    // frame ubo's - ring buffer slots for multi-pass rendering within a single frame
    static constexpr uint32_t            MAX_FRAME_SLOTS = 8;
    uint32_t                             _frameSlot      = 0;
    std::shared_ptr<ya::IDescriptorPool> _frameDSP;
    DescriptorSetHandle                  _frameDSs[MAX_FRAME_SLOTS];
    std::shared_ptr<ya::IBuffer>         _frameUBOs[MAX_FRAME_SLOTS];

    uint32_t getSlot() const { return _frameSlot; }
    void     advanceSlot() { _frameSlot = (_frameSlot + 1) % MAX_FRAME_SLOTS; }

    // material ubo's, dynamically extend
    uint32_t                                  _lastMaterialDSCount = 0;
    std::shared_ptr<ya::IDescriptorPool>      _materialDSP;
    std::vector<std::shared_ptr<ya::IBuffer>> _materialParamsUBOs;
    std::vector<DescriptorSetHandle>          _materialParamDSs;    // each material instance
    std::vector<DescriptorSetHandle>          _materialResourceDSs; // each material's texture

    std::string _ctxEntityDebugStr;

  public:
    UnlitMaterialSystem() : IMaterialSystem("UnlitMaterialSystem") {}

    void onInit(IRenderPass* renderPass, const PipelineRenderingInfo& pipelineRenderingInfo) override;
    void onDestroy() override {}
    void onRender(ICommandBuffer* cmdBuf, FrameContext* ctx) override;
    void resetFrameSlot() override { _frameSlot = 0; }
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void recreateMaterialDescPool(uint32_t count);

    void updateFrameDS(FrameContext* ctx);
    void updateMaterialParamDS(DescriptorSetHandle ds, UnlitMaterial* material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial* material);
};

} // namespace ya