#pragma once

#include "DeferredFrameGraphResources.h"
#include "DeferredGBufferResources.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/Core/Pipeline.h"
#include "RHI/Core/RenderImage.h"
#include "RHI/Core/Texture.h"
#include "Render3D/Stage/IRenderStage.h"

#include "DeferredRender.SSAO.slang.h"

#include <array>

namespace ya
{

struct IFrameBuffer;
struct DeferredSSAOPassParams;

struct YA_RENDER_3D_API SSAOStage : public IRenderStage
{
    using FrameData = slang_types::DeferredRender::SSAO::FrameData;

    static constexpr EFormat::T AO_FORMAT = EFormat::R8_UNORM;

    IRender*                 _render          = nullptr;
    DeferredGBufferResources _gBufferResources{};
    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _inputDSL;
    stdptr<IDescriptorPool>      _descriptorPool;

    // Kept alive for the pipeline layout; frame descriptor sets and upload
    // slices are owned by DeferredFrameResourceSet.
    stdptr<IDescriptorSetLayout> _frameDSL;
    struct FrameInputs
    {
        DescriptorSetHandle          descriptorSet{};
        FrameUploadArena::Allocation frame;

        [[nodiscard]] bool isValid() const
        {
            return descriptorSet && frame.valid();
        }
    };
    FrameInputs _frameInputs{};
    DescriptorSetHandle _inputDS = nullptr;

    stdptr<Texture> _noiseTexture;

    uint32_t                       _lastInputDescriptorWriteCount   = 0;
    float _radius = 0.6f;
    float _bias   = 0.025f;
    float _power  = 1.5f;
    float _intensity = 2.5f;
    bool  _bReverseY = true;

    SSAOStage() : IRenderStage("SSAO") {}

    void setup(const DeferredGBufferResources& gBufferResources);
    void refreshPipelineFormat();

    void init(IRender* render, stdptr<IDescriptorSetLayout> frameDSL);
    void init(IRender* render) override { init(render, nullptr); }
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;

    [[nodiscard]] FrameData buildFrameData(const RenderStageContext& ctx) const;
    void setFrameInputs(FrameInputs frameInputs)
    {
        _frameInputs = std::move(frameInputs);
    }

    RGTextureHandle appendGraphPass(RenderGraph& graph,
                                    const RenderStageContext& ctx,
                                    const DeferredSSAOPassParams& params);
    [[nodiscard]] float getRadius() const { return _radius; }
    [[nodiscard]] float getBias() const { return _bias; }
    [[nodiscard]] float getPower() const { return _power; }
    [[nodiscard]] float getIntensity() const { return _intensity; }
    [[nodiscard]] bool  isReverseYEnabled() const { return _bReverseY; }
    [[nodiscard]] uint32_t getLastInputDescriptorWriteCount() const { return _lastInputDescriptorWriteCount; }
    [[nodiscard]] IGraphicsPipeline* getPipeline() const { return _pipeline.get(); }
    void setSettings(float radius, float bias, float power, float intensity, bool bReverseY);

  private:
    void initNoiseTexture();
};

} // namespace ya
