#include "LightStage.h"
#include "Render/Core/RenderImage.h"
#include "Render/Render.h"

#include "Config/ConfigManager.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"
#include "Resource/Texture/TextureLibrary.h"

#include "imgui.h"

#include <string>
#include <vector>


namespace ya
{

namespace
{

constexpr const char* LIGHT_STAGE_CONFIG_DOC_NAME            = "editor";
constexpr const char* LIGHT_STAGE_CONFIG_KEY_PBR_DIFFUSE_IBL = "render.deferred.light.enablePBRDiffuseIBL";
constexpr const char* LIGHT_STAGE_CONFIG_KEY_PBR_SPECULAR_IBL = "render.deferred.light.enablePBRSpecularIBL";

std::vector<std::string> buildLightPassShaderDefines(bool bEnablePBRDiffuseIBL,
                                                     bool bEnablePBRSpecularIBL,
                                                     bool bEnableShadowMapping,
                                                     bool bEnablePointLightShadow)
{
    std::vector<std::string> defines = {
        std::string("YA_DEFERRED_PBR_ENABLE_IBL_DIFFUSE=") + (bEnablePBRDiffuseIBL ? "1" : "0"),
        std::string("YA_DEFERRED_PBR_ENABLE_IBL_SPECULAR=") + (bEnablePBRSpecularIBL ? "1" : "0"),
    };

    if (bEnableShadowMapping) {
        defines.push_back("YA_DEFERRED_ENABLE_SHADOW_MAPPING=1");
    }
    if (bEnablePointLightShadow) {
        defines.push_back("YA_DEFERRED_ENABLE_POINT_LIGHT_SHADOW=1");
    }

    return defines;
}

} // namespace

void LightStage::setIBLSettings(bool bEnablePBRDiffuseIBL, bool bEnablePBRSpecularIBL)
{
    const bool bChanged = _bEnablePBRDiffuseIBL != bEnablePBRDiffuseIBL ||
                          _bEnablePBRSpecularIBL != bEnablePBRSpecularIBL;

    _bEnablePBRDiffuseIBL  = bEnablePBRDiffuseIBL;
    _bEnablePBRSpecularIBL = bEnablePBRSpecularIBL;

    if (!bChanged || !_pipeline) {
        return;
    }

    auto ci               = _pipeline->getDesc();
    ci.shaderDesc.defines = buildLightPassShaderDefines(
        _bEnablePBRDiffuseIBL,
        _bEnablePBRSpecularIBL,
        _shadowState.bEnableShadowMapping,
        _shadowState.bEnablePointLightShadow);
    _pipeline->updateDesc(std::move(ci));
    _bShadowDescriptorsInitialized = false;
}

void LightStage::setup(SharedInputs sharedInputs, const DeferredGBufferResources& gBufferResources)
{
    _frameAndLightDSL = std::move(sharedInputs.frameAndLightDSL);
    _gBufferResources = gBufferResources;
    invalidateGBufferDescriptors();
}

void LightStage::setEnvironmentLightingInput(EnvironmentLightingInput input)
{
    _environmentLightingDSL = std::move(input.environmentLightingDSL);
    _getSceneEnvironmentLightingDescriptorSet = std::move(input.getSceneEnvironmentLightingDescriptorSet);
}

void LightStage::setFrameInputs(FrameInputs frameInputs)
{
    _frameInputs = frameInputs;
}

void LightStage::setSSAOTexture(std::shared_ptr<RenderImage> ssaoTexture)
{
    if (_ssaoTextureOwner == ssaoTexture) {
        return;
    }

    _ssaoTextureOwner = std::move(ssaoTexture);
    invalidateGBufferDescriptors();
}

void LightStage::applyShadowState(const ShadowRuntimeState& shadowState)
{
    const bool bDefinesChanged = _shadowState.bEnableShadowMapping != shadowState.bEnableShadowMapping ||
                                 _shadowState.bEnablePointLightShadow != shadowState.bEnablePointLightShadow;
    const bool bResourcesChanged = _shadowState.directionalDepthIV != shadowState.directionalDepthIV ||
                                   _shadowState.sampler != shadowState.sampler ||
                                   _shadowState.pointCubeDepthIVs != shadowState.pointCubeDepthIVs;

    _shadowState = shadowState;

    if (bResourcesChanged) {
        invalidateShadowDescriptors();
    }

    if (!bDefinesChanged || !_pipeline) {
        return;
    }

    auto ci               = _pipeline->getDesc();
    ci.shaderDesc.defines = buildLightPassShaderDefines(
        _bEnablePBRDiffuseIBL,
        _bEnablePBRSpecularIBL,
        _shadowState.bEnableShadowMapping,
        _shadowState.bEnablePointLightShadow);
    _pipeline->updateDesc(std::move(ci));
    _bShadowDescriptorsInitialized = false;
}

void LightStage::refreshPipelineFormats(const DeferredAttachmentFormats& formats)
{
    if (!_pipeline || !formats.hasColor()) {
        return;
    }

    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {formats.colorFormats.front()};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = formats.depthFormat.value_or(EFormat::Undefined);
    _pipeline->updateDesc(std::move(ci));
}

void LightStage::invalidateGBufferDescriptors()
{
    _lastGBufferImageViewHandles.fill(nullptr);
    _lastGBufferDepthImageViewHandle = nullptr;
    _lastSSAOImageViewHandle         = nullptr;
    _bGBufferDescriptorsInitialized  = false;
    _lastGBufferDescriptorWriteCount = 0;
}

void LightStage::invalidateShadowDescriptors()
{
    _lastShadowDirectionalImageViewHandle = nullptr;
    _lastShadowPointCubeImageViewHandles.fill(nullptr);
    _bShadowDescriptorsInitialized = false;
    _lastShadowDescriptorWriteCount = 0;
}

bool LightStage::shouldRefreshShadowDescriptors() const
{
    if (!_bShadowDescriptorsInitialized || !_shadowState.directionalDepthIV || !_shadowState.sampler) {
        return true;
    }

    if (_lastShadowDirectionalImageViewHandle != _shadowState.directionalDepthIV->getHandle()) {
        return true;
    }

    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        const auto currentHandle = _shadowState.pointCubeDepthIVs[lightIndex] ? _shadowState.pointCubeDepthIVs[lightIndex]->getHandle() : ImageViewHandle{};
        if (_lastShadowPointCubeImageViewHandles[lightIndex] != currentHandle) {
            return true;
        }
    }

    return false;
}

void LightStage::init(IRender* render)
{
    _render = render;
    YA_CORE_ASSERT(_frameAndLightDSL, "LightStage requires frame/light DSL (call setup() before init())");
    _fullscreenQuad = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad);
    YA_CORE_ASSERT(_fullscreenQuad != nullptr, "LightStage requires fullscreen quad mesh");

    auto& configManager      = ConfigManager::get();
    _bEnablePBRDiffuseIBL    = configManager.getOr<bool>(LIGHT_STAGE_CONFIG_DOC_NAME,
                                                      LIGHT_STAGE_CONFIG_KEY_PBR_DIFFUSE_IBL,
                                                      _bEnablePBRDiffuseIBL);
    _bEnablePBRSpecularIBL   = configManager.getOr<bool>(LIGHT_STAGE_CONFIG_DOC_NAME,
                                                      LIGHT_STAGE_CONFIG_KEY_PBR_SPECULAR_IBL,
                                                      _bEnablePBRSpecularIBL);

    // GBuffer texture DSL (set 1)
    _gBufferTextureDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_LightPass_GBuffer_DSL",
            .set      = 1,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 4, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        }});

    _shadowDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_LightPass_Shadow_DSL",
            .set      = 3,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = MAX_POINT_LIGHTS, .stageFlags = EShaderStage::Fragment},
            },
        }});

    // Pipeline layout: set 0 = frame+light (from GBufferStage), set 1 = GBuffer textures,
    // set 2 = environment lighting, set 3 = shadow maps
    _pipelineLayout = IPipelineLayout::create(
        _render,
        "Deferred_Light_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PushConstant), .stageFlags = EShaderStage::Vertex}},
        {
            _frameAndLightDSL,
            _gBufferTextureDSL,
            _environmentLightingDSL,
            _shadowDSL,
        });

    // Pipeline
    _pipelineCI = GraphicsPipelineCreateInfo{
        .pipelineRenderingInfo = {
            .label                  = "Deferred Light Pass",
            .colorAttachmentFormats = {LINEAR_FORMAT},
            .depthAttachmentFormat  = DEPTH_FORMAT,
        },
        .pipelineLayout = _pipelineLayout.get(),
        .shaderDesc     = ShaderDesc{
                .shaderName        = "DeferredRender/LightPass.slang",
                .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                .vertexAttributes  = _commonVertexAttributes,
                .defines           = buildLightPassShaderDefines(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL, _shadowState.bEnableShadowMapping, _shadowState.bEnablePointLightShadow),
        },
        .dynamicFeatures    = {EPipelineDynamicFeature::Viewport, EPipelineDynamicFeature::Scissor},
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = {.cullMode = ECullMode::None, .frontFace = EFrontFaceType::CounterClockWise},
        .depthStencilState  = {.bDepthTestEnable = false, .bDepthWriteEnable = false},
        .colorBlendState    = {.attachments = {ColorBlendAttachmentState{
                                   .index          = 0,
                                   .bBlendEnable   = false,
                                   .colorWriteMask = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                            }}},
        .viewportState      = {.viewports = {Viewport::defaults()}, .scissors = {Scissor::defaults()}},
    };
    _pipeline = IGraphicsPipeline::create(_render);
    YA_CORE_ASSERT(_pipeline && _pipeline->recreate(_pipelineCI), "Failed to create Light pipeline");

    // Descriptor pool for deferred light pass descriptor sets:
    //   1 set for GBuffer textures
    //   1 set for shadow maps

    _dsp = IDescriptorPool::create(
        _render, DescriptorPoolCreateInfo{
                     .label     = "LightStage_GBuffer_DSP",
                     .maxSets   = 2,
                     .poolSizes = {
                         {
                             .type            = EPipelineDescriptorType::CombinedImageSampler,
                             .descriptorCount = 5 + 1 + MAX_POINT_LIGHTS,
                        },
                    },
                });
    _gBufferTextureDS = _dsp->allocateDescriptorSets(_gBufferTextureDSL);
    _shadowDS         = _dsp->allocateDescriptorSets(_shadowDSL);
}

void LightStage::destroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _gBufferTextureDSL.reset();
    _shadowDSL.reset();
    _dsp.reset();
    _render                   = nullptr;
    _frameAndLightDSL.reset();
    _gBufferResources         = {};
    _ssaoTextureOwner.reset();
    _fullscreenQuad           = nullptr;
    _environmentLightingDSL.reset();
    _getSceneEnvironmentLightingDescriptorSet = {};
    _frameInputs = {};
    _shadowState              = {};
    _lastGBufferImageViewHandles.fill(nullptr);
    _lastGBufferDepthImageViewHandle = nullptr;
    _lastSSAOImageViewHandle         = nullptr;
    _lastShadowDirectionalImageViewHandle = nullptr;
    _lastShadowPointCubeImageViewHandles.fill(nullptr);
    _bGBufferDescriptorsInitialized  = false;
    _bShadowDescriptorsInitialized   = false;
    _lastGBufferDescriptorWriteCount = 0;
    _lastShadowDescriptorWriteCount  = 0;
}

void LightStage::prepare(const RenderStageContext& ctx)
{
    YA_PERF_SCOPE(perf::sample::deferredLightPrepare(), perf::metric::cpuTimeMs(), perf::domain::render());
    (void)ctx;
    if (_pipeline) {
        _pipeline->beginFrame();
    }

    if (!_gBufferResources.isComplete()) return;

    auto  sampler = TextureLibrary::get().getDefaultSampler();
    auto* albedo  = _gBufferResources.color[0];
    auto* normal  = _gBufferResources.color[1];
    auto* orm     = _gBufferResources.color[2];
    auto* shading = _gBufferResources.color[3];
    if (!albedo || !normal || !orm || !shading) {
        return;
    }

    const std::array<ImageViewHandle, 4> gbufferImageViewHandles = {
        albedo->getImageView() ? albedo->getImageView()->getHandle() : ImageViewHandle{},
        normal->getImageView() ? normal->getImageView()->getHandle() : ImageViewHandle{},
        orm->getImageView() ? orm->getImageView()->getHandle() : ImageViewHandle{},
        shading->getImageView() ? shading->getImageView()->getHandle() : ImageViewHandle{},
    };
    const auto gbufferDepthImageViewHandle = _gBufferResources.depth && _gBufferResources.depth->getImageView()
        ? _gBufferResources.depth->getImageView()->getHandle()
        : ImageViewHandle{};
    const auto ssaoImageViewHandle = _ssaoTextureOwner && _ssaoTextureOwner->getImageView()
        ? _ssaoTextureOwner->getImageView()->getHandle()
        : ImageViewHandle{};
    if (!_bGBufferDescriptorsInitialized ||
        _lastGBufferImageViewHandles != gbufferImageViewHandles ||
        _lastGBufferDepthImageViewHandle != gbufferDepthImageViewHandle ||
        _lastSSAOImageViewHandle != ssaoImageViewHandle) {
        auto* ssaoImageView = _ssaoTextureOwner
            ? _ssaoTextureOwner->getImageView()
            : TextureLibrary::get().getWhiteTexture()->getImageView();
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 0, albedo->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 1, normal->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 2, orm->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 3, shading->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 4, ssaoImageView, sampler.get()),
        });
        _lastGBufferImageViewHandles     = gbufferImageViewHandles;
        _lastGBufferDepthImageViewHandle = gbufferDepthImageViewHandle;
        _lastSSAOImageViewHandle         = ssaoImageViewHandle;
        _bGBufferDescriptorsInitialized  = true;
        _lastGBufferDescriptorWriteCount = 5;
    }
    else {
        _lastGBufferDescriptorWriteCount = 0;
    }

    if (_shadowState.bEnableShadowMapping && _shadowState.directionalDepthIV && _shadowState.sampler && shouldRefreshShadowDescriptors()) {
        std::vector<DescriptorImageInfo> pointShadowInfos(MAX_POINT_LIGHTS);
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            pointShadowInfos[lightIndex] = DescriptorImageInfo{
                .imageView   = _shadowState.pointCubeDepthIVs[lightIndex] ? _shadowState.pointCubeDepthIVs[lightIndex]->getHandle() : ImageViewHandle{},
                .sampler     = _shadowState.sampler->getHandle(),
                .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
            };
        }

        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(_shadowDS, 0, _shadowState.directionalDepthIV, _shadowState.sampler),
            WriteDescriptorSet{
                .dstSet          = _shadowDS,
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = MAX_POINT_LIGHTS,
                .imageInfos      = pointShadowInfos,
            },
        });
        _lastShadowDirectionalImageViewHandle = _shadowState.directionalDepthIV->getHandle();
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            _lastShadowPointCubeImageViewHandles[lightIndex] = _shadowState.pointCubeDepthIVs[lightIndex] ? _shadowState.pointCubeDepthIVs[lightIndex]->getHandle() : ImageViewHandle{};
        }
        _bShadowDescriptorsInitialized  = true;
        _lastShadowDescriptorWriteCount = 1 + MAX_POINT_LIGHTS;
    }
    else {
        _lastShadowDescriptorWriteCount = 0;
    }

}

void LightStage::execute(const RenderStageContext& ctx)
{
    YA_PERF_SCOPE(perf::sample::deferredLightExecute(), perf::metric::cpuTimeMs(), perf::domain::render());
    if (!ctx.cmdBuf || !_frameInputs.frameAndLightDescriptorSet || !_fullscreenQuad) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;

    cmdBuf->debugBeginLabel("LightStage");

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);

    // set 0 = frame+light (from GBufferStage), set 1 = GBuffer textures, set 2 = environment
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {
                                                             _frameInputs.frameAndLightDescriptorSet,
                                                             _gBufferTextureDS,
                                                             _frameInputs.environmentLightingDescriptorSet,
                                                             _shadowDS,
                                                         });

    _fullscreenQuad->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void LightStage::renderSettingsGUI()
{
    bool bEnablePBRDiffuseIBL  = _bEnablePBRDiffuseIBL;
    bool bEnablePBRSpecularIBL = _bEnablePBRSpecularIBL;
    bool bDirty                = false;
    bDirty |= ImGui::Checkbox("Enable PBR Diffuse IBL", &bEnablePBRDiffuseIBL);
    bDirty |= ImGui::Checkbox("Enable PBR Specular IBL", &bEnablePBRSpecularIBL);
    if (bDirty) {
        setIBLSettings(bEnablePBRDiffuseIBL, bEnablePBRSpecularIBL);
        ConfigManager::Editor(LIGHT_STAGE_CONFIG_DOC_NAME)
            .set(LIGHT_STAGE_CONFIG_KEY_PBR_DIFFUSE_IBL, _bEnablePBRDiffuseIBL)
            .set(LIGHT_STAGE_CONFIG_KEY_PBR_SPECULAR_IBL, _bEnablePBRSpecularIBL);
    }
}

void LightStage::renderTechnicalGUI()
{
    if (ImGui::TreeNode("Light Performance"))
    {
        auto& perf = PerfState::Get();
        ImGui::Text("Light prepare CPU: %.3f ms", perf.getLastValue(perf::sample::deferredLightPrepare(), perf::metric::cpuTimeMs()));
        ImGui::Text("Light execute CPU: %.3f ms", perf.getLastValue(perf::sample::deferredLightExecute(), perf::metric::cpuTimeMs()));
        ImGui::Text("Descriptor writes: gbuffer=%u shadow=%u", _lastGBufferDescriptorWriteCount, _lastShadowDescriptorWriteCount);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Light Pipeline")) {
        _pipeline->renderGUI();
        ImGui::TreePop();
    }
}

void LightStage::renderGUI()
{
    if (!ImGui::TreeNode("Lighting")) {
        return;
    }

    renderSettingsGUI();
    renderTechnicalGUI();

    ImGui::TreePop();
}

} // namespace ya
