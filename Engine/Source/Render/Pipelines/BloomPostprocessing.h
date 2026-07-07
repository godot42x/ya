#pragma once

#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Runtime/App/Common/PostProcessingState.h"

#include "Misc.BloomBlur.slang.h"
#include "Misc.BloomComposite.slang.h"
#include "Misc.BloomExtract.slang.h"

namespace ya
{

constexpr uint32_t MAX_BLOOM_BLUR_DESCRIPTOR_SETS = 64;

struct BloomPostprocessing
{
    struct InitDesc
    {
        IRender*              render                = nullptr;
        PipelineRenderingInfo pipelineRenderingInfo = {};
    };

    struct RenderDesc
    {
        ICommandBuffer*            cmdBuf            = nullptr;
        Texture*                   sceneTexture      = nullptr;
        Texture*                   outputTexture     = nullptr;
        Texture*                   bloomExtract      = nullptr;
        Texture*                   blurPingTexture   = nullptr;
        Texture*                   blurPongTexture   = nullptr;
        Extent2D                   renderExtent      = {};
        const PostProcessingState* state            = nullptr;
    };

    IRender* _render = nullptr;
    InitDesc _initDesc{};

    stdptr<IDescriptorSetLayout> _extractDSL;
    stdptr<IDescriptorPool>      _extractDSP;
    stdptr<IPipelineLayout>      _extractPPL;
    stdptr<IGraphicsPipeline>    _extractPipeline;
    DescriptorSetHandle          _extractDS = nullptr;
    ImageViewHandle              _extractInputImageViewHandle = nullptr;

    stdptr<IDescriptorSetLayout> _blurDSL;
    stdptr<IDescriptorPool>      _blurDSP;
    stdptr<IPipelineLayout>      _blurPPL;
    stdptr<IGraphicsPipeline>    _blurPipeline;
    std::vector<DescriptorSetHandle> _blurDSs;
    std::vector<ImageViewHandle>     _blurInputImageViewHandles;

    stdptr<IDescriptorSetLayout> _compositeDSL;
    stdptr<IDescriptorPool>      _compositeDSP;
    stdptr<IPipelineLayout>      _compositePPL;
    stdptr<IGraphicsPipeline>    _compositePipeline;
    DescriptorSetHandle          _compositeDS = nullptr;
    ImageViewHandle              _compositeSceneImageViewHandle = nullptr;
    ImageViewHandle              _compositeBloomImageViewHandle = nullptr;

    uint32_t _lastBlurPassCount = 0;

    void init(const InitDesc& initDesc);
    void shutdown();
    void beginFrame();
    void render(const RenderDesc& desc);
    void renderSettingsGUI(PostProcessingState& state);
    void renderTechnicalGUI();

  private:
    void initExtractPipeline();
    void initBlurPipeline();
    void initCompositePipeline();
    void updateExtractDescriptor(Texture* inputTexture);
    DescriptorSetHandle updateBlurDescriptor(uint32_t passIndex, Texture* inputTexture);
    void updateCompositeDescriptor(Texture* sceneTexture, Texture* bloomTexture);
};

} // namespace ya
