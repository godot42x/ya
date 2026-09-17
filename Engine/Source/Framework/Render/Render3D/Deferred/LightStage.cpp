#include "Render3D/Deferred/LightStage.h"
#include "RHI/Render.h"
#include "RHI/Backend/TextureLibrary.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Profiling/PerfKeys.h"
#include "Core/Profiling/PerfState.h"
#include "Resource/Mesh/PrimitiveMeshCache.h"

#include <string>
#include <vector>


namespace ya
{

namespace
{

constexpr const char* LIGHT_STAGE_CONFIG_DOC_NAME            = "runtime";
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
}

void LightStage::setup(SharedInputs sharedInputs)
{
    _frameAndLightDSL = std::move(sharedInputs.frameAndLightDSL);
}

void LightStage::setEnvironmentLightingInput(EnvironmentLightingInput input)
{
    _environmentLightingDSL = std::move(input.environmentLightingDSL);
}

void LightStage::setFrameInputs(FrameInputs frameInputs)
{
    _frameInputs = frameInputs;
}

void LightStage::applyShadowState(const ShadowRuntimeState& shadowState)
{
    const bool bDefinesChanged = _shadowState.bEnableShadowMapping != shadowState.bEnableShadowMapping ||
                                 _shadowState.bEnablePointLightShadow != shadowState.bEnablePointLightShadow;

    _shadowState = shadowState;

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
}

void LightStage::destroy()
{
    _pipeline.reset();
    _pipelineLayout.reset();
    _gBufferTextureDSL.reset();
    _shadowDSL.reset();
    _render = nullptr;
    _frameAndLightDSL.reset();
    _fullscreenQuad = nullptr;
    _environmentLightingDSL.reset();
    _frameInputs = {};
    _shadowState = {};
}

void LightStage::prepare(const RenderStageContext& ctx)
{
    YA_PERF_SCOPE(perf::sample::deferredLightPrepare(), perf::metric::cpuTimeMs(), perf::domain::render());
    (void)ctx;
    if (_pipeline) {
        _pipeline->beginFrame();
    }
}

void LightStage::writeShadowDescriptors(DescriptorSetHandle shadowDS) const
{
    if (!_render || !shadowDS || !_shadowState.bEnableShadowMapping ||
        !_shadowState.directionalDepthIV || !_shadowState.sampler) {
        return;
    }

    std::vector<DescriptorImageInfo> pointShadowInfos(MAX_POINT_LIGHTS);
    for (uint32_t lightIndex = 0; lightIndex < MAX_POINT_LIGHTS; ++lightIndex) {
        pointShadowInfos[lightIndex] = DescriptorImageInfo{
            .imageView   = _shadowState.pointCubeDepthIVs[lightIndex] ? _shadowState.pointCubeDepthIVs[lightIndex]->getHandle() : ImageViewHandle{},
            .sampler     = _shadowState.sampler->getHandle(),
            .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
        };
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::writeOneImage(shadowDS, 0, _shadowState.directionalDepthIV, _shadowState.sampler),
        WriteDescriptorSet{
            .dstSet          = shadowDS,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount = MAX_POINT_LIGHTS,
            .imageInfos      = pointShadowInfos,
        },
    });
}

void LightStage::writeGBufferTextureDescriptors(
    DescriptorSetHandle                          gBufferTextureDS,
    const RGRenderContext::RGPassBindingContext& binding,
    RGTextureHandle                              albedo,
    RGTextureHandle                              normal,
    RGTextureHandle                              orm,
    RGTextureHandle                              shading,
    RGTextureHandle                              depth,
    std::optional<RGTextureHandle>               ssao) const
{
    if (!_render || !gBufferTextureDS) {
        return;
    }

    auto sampler = TextureLibrary::get().getDefaultSampler();
    const auto albedoInfo = binding.resolveTextureDescriptor(albedo, sampler.get());
    const auto normalInfo = binding.resolveTextureDescriptor(normal, sampler.get());
    const auto ormInfo    = binding.resolveTextureDescriptor(orm, sampler.get());
    const auto shadingInfo = binding.resolveTextureDescriptor(shading, sampler.get());
    const auto depthInfo  = binding.resolveTextureDescriptor(depth, sampler.get());
    auto       ssaoInfo   = ssao.has_value()
        ? binding.resolveTextureDescriptor(*ssao, sampler.get())
        : std::optional<DescriptorImageInfo>{};
    if (!ssaoInfo) {
        auto whiteTexture = TextureLibrary::get().getWhiteTexture();
        if (whiteTexture && whiteTexture->getImageView()) {
            ssaoInfo = DescriptorImageInfo{
                .imageView   = whiteTexture->getImageView()->getHandle(),
                .sampler     = sampler->getHandle(),
                .imageLayout = EImageLayout::ShaderReadOnlyOptimal,
            };
        }
    }
    if (!albedoInfo || !normalInfo || !ormInfo || !shadingInfo || !depthInfo || !ssaoInfo) {
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genImageWrite(gBufferTextureDS, 0, 0, EPipelineDescriptorType::CombinedImageSampler, {*albedoInfo}),
        IDescriptorSetHelper::genImageWrite(gBufferTextureDS, 1, 0, EPipelineDescriptorType::CombinedImageSampler, {*normalInfo}),
        IDescriptorSetHelper::genImageWrite(gBufferTextureDS, 2, 0, EPipelineDescriptorType::CombinedImageSampler, {*ormInfo}),
        IDescriptorSetHelper::genImageWrite(gBufferTextureDS, 3, 0, EPipelineDescriptorType::CombinedImageSampler, {*shadingInfo}),
        IDescriptorSetHelper::genImageWrite(gBufferTextureDS, 4, 0, EPipelineDescriptorType::CombinedImageSampler, {*ssaoInfo}),
    });
}

void LightStage::execute(const RenderStageContext& ctx,
                         DescriptorSetHandle       frameAndLight,
                         DescriptorSetHandle       environmentLighting,
                         DescriptorSetHandle       gBufferTextures,
                         DescriptorSetHandle       shadows)
{
    YA_PERF_SCOPE(perf::sample::deferredLightExecute(), perf::metric::cpuTimeMs(), perf::domain::render());
    if (!ctx.cmdBuf || !frameAndLight || !_fullscreenQuad) return;

    auto* cmdBuf = ctx.cmdBuf;
    auto  vpW    = ctx.viewportExtent.width;
    auto  vpH    = ctx.viewportExtent.height;

    cmdBuf->debugBeginLabel("LightStage");

    cmdBuf->bindPipeline(_pipeline.get());
    cmdBuf->setViewport(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));
    cmdBuf->setScissor(0, 0, vpW, vpH);

    cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, {
                                                             frameAndLight,
                                                             gBufferTextures,
                                                             environmentLighting,
                                                             shadows,
                                                         });

    _fullscreenQuad->draw(cmdBuf);

    cmdBuf->debugEndLabel();
}

void LightStage::execute(const RenderStageContext& ctx)
{
    execute(ctx,
            _frameInputs.frameAndLightDescriptorSet,
            _frameInputs.environmentLightingDescriptorSet,
            {},
            {});
}

} // namespace ya
