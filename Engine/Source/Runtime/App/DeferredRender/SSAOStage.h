#pragma once

#include "DeferredGBufferResources.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderImage.h"
#include "Render/Core/Texture.h"
#include "Render/Stage/IRenderStage.h"

#include "DeferredRender.SSAO.slang.h"

#include <array>

namespace ya
{

struct IRenderTarget;
struct IFrameBuffer;

struct SSAOStage : public IRenderStage
{
    using FrameData = slang_types::DeferredRender::SSAO::FrameData;

    static constexpr EFormat::T AO_FORMAT = EFormat::R8_UNORM;

    IRender*                 _render          = nullptr;
    DeferredGBufferResources _gBufferResources{};
    const RenderImage*       _outputTexture   = nullptr;

    stdptr<IGraphicsPipeline>    _pipeline;
    stdptr<IPipelineLayout>      _pipelineLayout;
    stdptr<IDescriptorSetLayout> _frameDSL;
    stdptr<IDescriptorSetLayout> _inputDSL;
    stdptr<IDescriptorPool>      _descriptorPool;

    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> _frameUBO{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> _frameDS{};
    DescriptorSetHandle _inputDS = nullptr;

    stdptr<Texture> _noiseTexture;

    std::array<ImageViewHandle, 4> _lastGBufferImageViewHandles{};
    ImageViewHandle                _lastGBufferDepthImageViewHandle = nullptr;
    bool                           _bInputDescriptorsInitialized    = false;
    uint32_t                       _lastInputDescriptorWriteCount   = 0;
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    float _radius = 0.6f;
    float _bias   = 0.025f;
    float _power  = 1.5f;
    float _intensity = 2.5f;
    bool  _bReverseY = true;

    SSAOStage() : IRenderStage("SSAO") {}

    void setup(const DeferredGBufferResources& gBufferResources);
    void refreshPipelineFormat();
    void invalidateInputDescriptors();

    void init(IRender* render) override;
    void destroy() override;
    void prepare(const RenderStageContext& ctx) override;
    void execute(const RenderStageContext& ctx) override;
    void renderGUI() override;
    void renderSettingsGUI() override;
    void renderTechnicalGUI() override;

    [[nodiscard]] const RenderImage* getOutputTexture() const { return _outputTexture; }
    [[nodiscard]] float getRadius() const { return _radius; }
    [[nodiscard]] float getBias() const { return _bias; }
    [[nodiscard]] float getPower() const { return _power; }
    [[nodiscard]] float getIntensity() const { return _intensity; }
    void setSettings(float radius, float bias, float power, float intensity, bool bReverseY);

  private:
    void initNoiseTexture();
    void updateFrameUBO(const RenderStageContext& ctx);
    void updateInputDescriptors();
};

} // namespace ya
