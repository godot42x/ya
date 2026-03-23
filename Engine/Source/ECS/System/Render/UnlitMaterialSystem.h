#pragma once
#include "Core/Base.h"

#include "Core/System/System.h"
#include "ECS/System/Render/IMaterialSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/Texture.h"
#include "Render/Material/MaterialDescPool.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/RenderDefines.h"

#include "Test.Unlit.glsl.h"


namespace ya
{

struct UnlitMaterial;


struct UnlitMaterialSystem : public IMaterialSystem
{

    using material_param_t = UnlitMaterial::ParamUBO;

    // Use shader companion types directly for non-material UBOs (avoids alignment bugs)
    using FrameUBO = glsl_types::Test::Unlit::FrameUBO;

    struct PushConstant
    {
        alignas(16) glm::mat4 modelMatrix{1.0f};
        alignas(16) glm::mat3 normalMatrix{1.0f};
    };


    static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
    static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;

    GraphicsPipelineCreateInfo _pipelineDesc;
    stdptr<IPipelineLayout>    _pipelineLayout = nullptr;


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

    // material descriptor pool (replaces manual DS management)
    MaterialDescPool<UnlitMaterial, UnlitMaterial::ParamUBO> _matPool;
    bool _bDescriptorPoolRecreated = false;

    std::string _ctxEntityDebugStr;

  public:
    UnlitMaterialSystem() : IMaterialSystem("UnlitMaterialSystem") {}

    void onInitImpl(const InitParams& initParams) override;
    void onDestroy() override {}
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;
    void resetFrameSlot() override { _frameSlot = 0; }
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }


  private:
    void preTick(float deltaTime, const FrameContext* ctx);
    void updateFrameDS(const FrameContext* ctx);
    void updateMaterialParamUBO(IBuffer* paramUBO, UnlitMaterial* material);
    void updateMaterialResourceDS(DescriptorSetHandle ds, UnlitMaterial* material);
};

} // namespace ya