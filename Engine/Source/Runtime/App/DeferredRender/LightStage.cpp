#include "LightStage.h"

#include "Config/ConfigManager.h"
#include "Core/Debug/PerfKeys.h"
#include "Core/Debug/PerfState.h"
#include "GBufferStage.h"
#include "Resource/PrimitiveMeshCache.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"

#include <string>


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
        _bEnableShadowMapping,
        _bEnablePointLightShadow);
    _pipeline->updateDesc(std::move(ci));
    _bShadowDescriptorsInitialized = false;
}

void LightStage::setup(GBufferStage* gBufferStage, IRenderTarget* gBufferRT)
{
    _gBufferStage = gBufferStage;
    _gBufferRT    = gBufferRT;
}

void LightStage::setShadowResources(IImageView*                                      directionalDepthIV,
                                    const std::array<IImageView*, MAX_POINT_LIGHTS>& pointCubeDepthIVs,
                                    Sampler*                                         shadowSampler)
{
    _shadowDirectionalDepthIV = directionalDepthIV;
    _shadowPointCubeIVs       = pointCubeDepthIVs;
    _shadowSampler            = shadowSampler;
    _bShadowDescriptorsInitialized = false;
}

void LightStage::setShadowSettings(bool bEnableShadowMapping, bool bEnablePointLightShadow)
{
    const bool bChanged = _bEnableShadowMapping != bEnableShadowMapping ||
                          _bEnablePointLightShadow != bEnablePointLightShadow;

    _bEnableShadowMapping   = bEnableShadowMapping;
    _bEnablePointLightShadow = bEnablePointLightShadow;

    if (!bChanged || !_pipeline) {
        return;
    }

    auto ci               = _pipeline->getDesc();
    ci.shaderDesc.defines = buildLightPassShaderDefines(
        _bEnablePBRDiffuseIBL,
        _bEnablePBRSpecularIBL,
        _bEnableShadowMapping,
        _bEnablePointLightShadow);
    _pipeline->updateDesc(std::move(ci));
    _bShadowDescriptorsInitialized = false;
}

void LightStage::refreshPipelineFormats(const IRenderTarget* viewportRT)
{
    if (!_pipeline || !viewportRT) {
        return;
    }

    const auto& colorDescs = viewportRT->getColorAttachmentDescs();
    const auto& depthDesc  = viewportRT->getDepthAttachmentDesc();
    if (colorDescs.empty()) {
        return;
    }

    auto ci                                         = _pipeline->getDesc();
    ci.pipelineRenderingInfo.colorAttachmentFormats = {colorDescs.front().format};
    ci.pipelineRenderingInfo.depthAttachmentFormat  = depthDesc.has_value() ? depthDesc->format : EFormat::Undefined;
    _pipeline->updateDesc(std::move(ci));
}

void LightStage::init(IRender* render)
{
    _render = render;
    YA_CORE_ASSERT(_gBufferStage, "LightStage requires GBufferStage (call setup() before init())");

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
    auto* runtime   = App::get()->getRenderRuntime();
    _pipelineLayout = IPipelineLayout::create(
        _render,
        "Deferred_Light_PPL",
        {PushConstantRange{.offset = 0, .size = sizeof(PushConstant), .stageFlags = EShaderStage::Vertex}},
        {
            _gBufferStage->getFrameAndLightDSL(),
            _gBufferTextureDSL,
            runtime->getEnvironmentLightingDescriptorSetLayout(),
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
                .shaderName        = "DeferredRender/Unified_LightPass.slang",
                .vertexBufferDescs = {VertexBufferDescription{.slot = 0, .pitch = sizeof(ya::Vertex)}},
                .vertexAttributes  = _commonVertexAttributes,
                .defines           = buildLightPassShaderDefines(_bEnablePBRDiffuseIBL, _bEnablePBRSpecularIBL, _bEnableShadowMapping, _bEnablePointLightShadow),
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
                             .descriptorCount = 4 + 1 + MAX_POINT_LIGHTS,
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
    _gBufferStage             = nullptr;
    _gBufferRT                = nullptr;
    _shadowDirectionalDepthIV = nullptr;
    _shadowPointCubeIVs.fill(nullptr);
    _shadowSampler                   = nullptr;
    _lastGBufferFrameBuffer          = nullptr;
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

    if (!_gBufferRT) return;

    // Update GBuffer texture DS from current GBuffer RT frame buffer
    auto  sampler = TextureLibrary::get().getDefaultSampler();
    auto* fb      = _gBufferRT->getCurFrameBuffer();
    if (!fb) {
        return;
    }

    if (!_bGBufferDescriptorsInitialized || _lastGBufferFrameBuffer != fb) {
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 0, fb->getColorTexture(0)->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 1, fb->getColorTexture(1)->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 2, fb->getColorTexture(2)->getImageView(), sampler.get()),
            IDescriptorSetHelper::writeOneImage(_gBufferTextureDS, 3, fb->getColorTexture(3)->getImageView(), sampler.get()),
        });
        _lastGBufferFrameBuffer          = fb;
        _bGBufferDescriptorsInitialized  = true;
        _lastGBufferDescriptorWriteCount = 4;
    }
    else {
        _lastGBufferDescriptorWriteCount = 0;
    }

    if (_bEnableShadowMapping && _shadowDirectionalDepthIV && _shadowSampler && !_bShadowDescriptorsInitialized) {
        std::vector<DescriptorImageInfo> pointShadowInfos(MAX_POINT_LIGHTS);
        for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
            pointShadowInfos[lightIndex] = DescriptorImageInfo{
                .imageView   = _shadowPointCubeIVs[lightIndex] ? _shadowPointCubeIVs[lightIndex]->getHandle() : ImageViewHandle{},
                .sampler     = _shadowSampler->getHandle(),
                .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
            };
        }

        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(_shadowDS, 0, _shadowDirectionalDepthIV, _shadowSampler),
            WriteDescriptorSet{
                .dstSet          = _shadowDS,
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                .descriptorCount = MAX_POINT_LIGHTS,
                .imageInfos      = pointShadowInfos,
            },
        });
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
    if (!ctx.cmdBuf || !_gBufferStage) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;

    // Get environment lighting DS from RenderRuntime
    auto* runtime               = App::get()->getRenderRuntime();
    auto  environmentLightingDS = runtime ? runtime->getSceneEnvironmentLightingDescriptorSet() : nullptr;

    cmdBuf->debugBeginLabel("LightStage");

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);

    // set 0 = frame+light (from GBufferStage), set 1 = GBuffer textures, set 2 = environment
    auto frameAndLightDS = _gBufferStage->getFrameAndLightDS(ctx.flightIndex);
    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {
                                                             frameAndLightDS,
                                                             _gBufferTextureDS,
                                                             environmentLightingDS,
                                                             _shadowDS,
                                                         });

    PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Quad)->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void LightStage::renderGUI()
{
    if (!ImGui::TreeNode("LightStage")) {
        return;
    }

    if (ImGui::TreeNode("Settings"))
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
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Performance"))
    {
        auto& perf = PerfState::Get();
        ImGui::Text("Light prepare CPU: %.3f ms", perf.getLastValue(perf::sample::deferredLightPrepare(), perf::metric::cpuTimeMs()));
        ImGui::Text("Light execute CPU: %.3f ms", perf.getLastValue(perf::sample::deferredLightExecute(), perf::metric::cpuTimeMs()));
        ImGui::Text("Descriptor writes: gbuffer=%u shadow=%u", _lastGBufferDescriptorWriteCount, _lastShadowDescriptorWriteCount);
        ImGui::TreePop();
    }

    _pipeline->renderGUI();
    ImGui::TreePop();
}

} // namespace ya
