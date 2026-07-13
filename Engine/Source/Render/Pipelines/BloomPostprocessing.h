#pragma once

#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Pipeline.h"
#include "Render/Core/RenderGraphExecutor.h"
#include "Render/Core/RenderImage.h"
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
        IImageView*                sceneImageView    = nullptr;
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
    const RenderImage* _extractImage = nullptr;
    const RenderImage* _blurPingImage = nullptr;
    const RenderImage* _blurPongImage = nullptr;
    const RenderImage* _compositeImage = nullptr;
    std::unique_ptr<RenderGraphExecutor> _graphExecutor;

    void init(const InitDesc& initDesc);
    void shutdown();
    void beginFrame();
    void render(const RenderDesc& desc);
    void renderSettingsGUI(PostProcessingState& state);
    void renderTechnicalGUI();
    [[nodiscard]] RenderImage* getExtractImage() const { return const_cast<RenderImage*>(_extractImage); }
    [[nodiscard]] RenderImage* getBlurImage() const { return const_cast<RenderImage*>(_blurPongImage ? _blurPongImage : _blurPingImage); }
    [[nodiscard]] RenderImage* getCompositeImage() const { return const_cast<RenderImage*>(_compositeImage); }

  private:
    void initExtractPipeline();
    void initBlurPipeline();
    void initCompositePipeline();
    void updateExtractDescriptor(IImageView* inputImageView);
    DescriptorSetHandle updateBlurDescriptor(uint32_t passIndex, IImageView* inputImageView);
    void updateCompositeDescriptor(IImageView* sceneImageView, IImageView* bloomImageView);
};

} // namespace ya
