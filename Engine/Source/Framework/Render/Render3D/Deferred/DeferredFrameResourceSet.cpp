#include "DeferredFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Render.h"
#include "Render3D/Common/FrameResourceSubmission.h"

#include <algorithm>
#include <limits>

namespace ya
{

void DeferredFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "DeferredFrameResourceSet requires a render backend");
    YA_CORE_ASSERT(render->getResourceFactory() != nullptr, "DeferredFrameResourceSet requires a resource factory");

    initSkinnedUploadArena(render, "Deferred", "Deferred_Skinning_DSL", 3, "Deferred.FrameUpload");

    _frameAndLightDSL = IDescriptorSetLayout::create(
        _render,
        {DescriptorSetLayoutDesc{
            .label    = "Deferred_Frame_And_Light_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::All},
            },
        }});
    _frameAndLightSets.init(_render, "Deferred_Frame_And_Light_DSP", 2);

    _ssaoFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });
    _ssaoFrameSets.init(_render, "Deferred_SSAO_Frame_DSP", 1);

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skybox_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    _skyboxFrameSets.init(_render, "Deferred_Skybox_Frame_DSP", 1);

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "DeferredFrameResourceSet failed to create initial skinning resources");
}

void DeferredFrameResourceSet::destroy()
{
    _viewBindings.clear();
    _skinningBindings = {};
    destroySkinnedUploadArena();
    _ssaoFrameSets.destroy();
    _ssaoFrameDSL.reset();
    _skyboxFrameSets.destroy();
    _skyboxFrameDSL.reset();
    _frameAndLightSets.destroy();
    _frameAndLightDSL.reset();
    _shadowState = {};
    _lastShadowedPointLights = 0;
}

DeferredFrameResourceSet::LightData DeferredFrameResourceSet::buildLightData(const RenderFrameData& frameData) const
{
    LightData lightData{};
    lightData.hasDirLight              = 0;
    lightData.dirLight.bias            = _shadowState.bias;
    lightData.dirLight.normalBias      = _shadowState.normalBias;
    lightData.dirLight.shadowFilter    = static_cast<uint32_t>(_shadowState.filter);
    lightData.dirLight.shadowTexelSize = _shadowState.shadowMapResolution > 0
        ? 1.0f / static_cast<float>(_shadowState.shadowMapResolution)
        : 0.0f;

    if (frameData.sceneSnapshot && frameData.sceneSnapshot->bHasDirectionalLight) {
        lightData.dirLight.dir          = frameData.directionalLight.direction;
        lightData.dirLight.color        = frameData.directionalLight.color;
        lightData.dirLight.intensity    = frameData.directionalLight.intensity;
        lightData.dirLight.cascadeCount = frameData.directionalLight.cascadeCount;
        for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_DIRECTIONAL_CASCADES; ++cascadeIndex) {
            lightData.dirLight.shadowMatrices[cascadeIndex] = frameData.directionalLight.cascadeViewProjections[cascadeIndex];
            lightData.dirLight.cascadeSplits[cascadeIndex]  = frameData.directionalLight.cascadeSplits[cascadeIndex];
        }
        lightData.hasDirLight = 1;
    }

    int            pointLightIndex        = 0;
    const uint32_t shadowedPointLightBudget = std::min(_shadowState.maxShadowedPointLights, frameData.numPointLights);
    for (uint32_t sourceIndex = 0;
         sourceIndex < frameData.numPointLights && pointLightIndex < static_cast<int>(MAX_POINT_LIGHTS);
         ++sourceIndex) {
        const auto& source = frameData.pointLights[sourceIndex];
        lightData.pointLights[pointLightIndex] = {
            .pos       = source.position,
            .color     = source.color,
            .intensity = source.intensity,
            .farPlane  = static_cast<uint32_t>(pointLightIndex) < shadowedPointLightBudget ? source.farPlane : 0.0f,
        };
        ++pointLightIndex;
    }
    lightData.numPointLight = static_cast<uint32_t>(pointLightIndex);
    return lightData;
}

std::optional<uint32_t> DeferredFrameResourceSet::calculateSkinningCapacity(
    uint32_t currentCapacity,
    uint32_t paletteCount)
{
    return PerFlightFrameResourceSetBase<DeferredFrameResourceSet>::calculateSkinningCapacity(
        currentCapacity,
        paletteCount);
}

bool DeferredFrameResourceSet::ensureViewDescriptors(Binding& binding)
{
    if (binding.frameAndLightDescriptorSet && binding.ssaoFrameDescriptorSet &&
        binding.skyboxFrameDescriptorSet) {
        return true;
    }

    if (!binding.frameAndLightDescriptorSet) {
        binding.frameAndLightDescriptorSet = _frameAndLightSets.allocate(_frameAndLightDSL);
    }
    if (!binding.ssaoFrameDescriptorSet) {
        binding.ssaoFrameDescriptorSet = _ssaoFrameSets.allocate(_ssaoFrameDSL);
    }
    if (!binding.skyboxFrameDescriptorSet) {
        binding.skyboxFrameDescriptorSet = _skyboxFrameSets.allocate(_skyboxFrameDSL);
    }

    return binding.frameAndLightDescriptorSet && binding.ssaoFrameDescriptorSet &&
           binding.skyboxFrameDescriptorSet;
}

void DeferredFrameResourceSet::updateFrameAndLightDescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.frameAndLightDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.frame.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            binding.frameAndLightDescriptorSet,
            1,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.light.descriptor()}),
    });
}

void DeferredFrameResourceSet::updateSSAODescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.ssaoFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.ssaoFrame.descriptor()}),
    });
}

void DeferredFrameResourceSet::updateSkyboxDescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.skyboxFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.skyboxFrame.descriptor()}),
    });
}

bool DeferredFrameResourceSet::beginSubmission(const RenderSubmissionContext& submission)
{
    if (!_render) {
        return false;
    }
    return beginFrameResourceSubmission(_uploadArena.get(), _viewBindings, submission);
}

bool DeferredFrameResourceSet::writeViewPayloads(
    FrameUploadArena&   arena,
    uint32_t            flightIndex,
    uint32_t            alignment,
    const ViewPayloads& payloads,
    Binding&            binding)
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT || alignment == 0) {
        return false;
    }

    auto frame = writeUploadSlice(arena, flightIndex, alignment, &payloads.frame, sizeof(payloads.frame));
    auto light = writeUploadSlice(arena, flightIndex, alignment, &payloads.light, sizeof(payloads.light));
    if (!frame || !light) {
        return false;
    }

    binding.frame = *frame;
    binding.light = *light;

    if (payloads.ssao) {
        auto ssaoFrame = writeUploadSlice(
            arena, flightIndex, alignment, payloads.ssao, sizeof(*payloads.ssao));
        if (!ssaoFrame) {
            return false;
        }
        binding.ssaoFrame = *ssaoFrame;
    }
    if (payloads.skybox) {
        auto skyboxFrame = writeUploadSlice(
            arena, flightIndex, alignment, payloads.skybox, sizeof(*payloads.skybox));
        if (!skyboxFrame) {
            return false;
        }
        binding.skyboxFrame = *skyboxFrame;
    }
    return true;
}

const DeferredFrameResourceSet::Binding* DeferredFrameResourceSet::beginView(
    const RenderSubmissionContext& submission,
    RenderViewRecordingContext&    view,
    const SSAOFrameData*           ssao,
    const SkyboxFrameData*         skybox)
{
    if (!_render || !_uploadArena || submission.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return nullptr;
    }
    if (!view.frameData) {
        YA_CORE_ERROR("Deferred beginView requires view frame data");
        return nullptr;
    }

    Binding* slot = _viewBindings.mutableNextView(submission.flightIndex);
    if (!slot) {
        YA_CORE_ERROR("Deferred beginView requires beginSubmission on flight {}", submission.flightIndex);
        return nullptr;
    }

    const uint32_t viewSlot = _viewBindings.liveViewCount(submission.flightIndex);
    const SkinningBinding& skinning = _skinningBindings[submission.flightIndex];
    slot->skinningDescriptorSet = skinning.skinningDescriptorSet;
    slot->skinningBuffer        = skinning.skinningBuffer;

    if (!ensureViewDescriptors(*slot)) {
        YA_CORE_ERROR("Deferred beginView failed to allocate view descriptor sets");
        return nullptr;
    }

    ViewPayloads payloads{};
    payloads.frame = FrameData{
        .viewPos    = view.frameData->cameraPos,
        .viewMatrix = view.frameData->view,
        .projMatrix = view.frameData->projection,
    };
    payloads.light  = buildLightData(*view.frameData);
    payloads.ssao   = ssao;
    payloads.skybox = skybox;
    _lastShadowedPointLights = std::min({
        _shadowState.maxShadowedPointLights,
        view.frameData->numPointLights,
        static_cast<uint32_t>(MAX_POINT_LIGHTS),
    });

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    if (!writeViewPayloads(*_uploadArena, submission.flightIndex, alignment, payloads, *slot)) {
        YA_CORE_ERROR("Deferred beginView failed to upload view payloads");
        return nullptr;
    }

    updateFrameAndLightDescriptorSet(*slot);
    if (payloads.ssao) {
        updateSSAODescriptorSet(*slot);
    }
    if (payloads.skybox) {
        updateSkyboxDescriptorSet(*slot);
    }

    if (!_viewBindings.commitNextView(submission.flightIndex)) {
        return nullptr;
    }

    view.viewSlot = viewSlot;
    return _viewBindings.getView(submission.flightIndex, viewSlot);
}

const DeferredFrameResourceSet::Binding* DeferredFrameResourceSet::getViewBinding(
    uint32_t flightIndex,
    uint32_t viewSlot) const
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

uint32_t DeferredFrameResourceSet::liveViewCount(uint32_t flightIndex) const
{
    return _viewBindings.liveViewCount(flightIndex);
}

} // namespace ya
