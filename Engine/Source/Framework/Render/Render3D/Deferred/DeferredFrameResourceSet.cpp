#include "Render3D/Deferred/DeferredFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Render.h"
#include "Render3D/Common/FrameResourceSubmission.h"
#include "Render3D/Common/SceneFamilyResources.h"

#include <algorithm>
#include <limits>

namespace ya
{

void DeferredFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "DeferredFrameResourceSet requires a render backend");
    YA_CORE_ASSERT(render->getResourceFactory() != nullptr, "DeferredFrameResourceSet requires a resource factory");

    initSkinningLayout(render, "Deferred", "Deferred_Skinning_DSL", 3);

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

    _ssaoFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skybox_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
}

void DeferredFrameResourceSet::destroy()
{
    _viewBindings.clear();
    destroySkinningLayout();
    _ssaoFrameDSL.reset();
    _skyboxFrameDSL.reset();
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

bool DeferredFrameResourceSet::prepareSkinning(
    RenderSubmission&                 submission,
    const RenderViewRecordingContext& view)
{
    SceneFamilyResources* family = allocateSceneFamilyForView(submission, view);
    if (!family || !view.frameData) {
        return false;
    }
    return prepareSceneFamilySkinning(
        submission, *family, view.frameData->sceneResources.skinningBuffer, _render, _skinningDSL);
}

bool DeferredFrameResourceSet::ensureViewDescriptors(RenderSubmission& submission, Binding& binding)
{
    if (binding.frameAndLightDescriptorSet && binding.ssaoFrameDescriptorSet &&
        binding.skyboxFrameDescriptorSet) {
        return true;
    }

    if (!binding.frameAndLightDescriptorSet) {
        binding.frameAndLightDescriptorSet = submission.allocateDescriptorSet(_frameAndLightDSL, 2);
    }
    if (!binding.ssaoFrameDescriptorSet) {
        binding.ssaoFrameDescriptorSet = submission.allocateDescriptorSet(_ssaoFrameDSL, 1);
    }
    if (!binding.skyboxFrameDescriptorSet) {
        binding.skyboxFrameDescriptorSet = submission.allocateDescriptorSet(_skyboxFrameDSL, 1);
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

bool DeferredFrameResourceSet::writeViewPayloads(
    RenderSubmission&   submission,
    uint32_t            alignment,
    const ViewPayloads& payloads,
    Binding&            binding)
{
    if (!submission.isRecording() || alignment == 0) {
        return false;
    }

    auto frame = writeUploadSlice(submission, alignment, &payloads.frame, sizeof(payloads.frame));
    auto light = writeUploadSlice(submission, alignment, &payloads.light, sizeof(payloads.light));
    if (!frame || !light) {
        return false;
    }

    binding.frame = *frame;
    binding.light = *light;

    if (payloads.ssao) {
        auto ssaoFrame = writeUploadSlice(submission, alignment, payloads.ssao, sizeof(*payloads.ssao));
        if (!ssaoFrame) {
            return false;
        }
        binding.ssaoFrame = *ssaoFrame;
    }
    if (payloads.skybox) {
        auto skyboxFrame = writeUploadSlice(submission, alignment, payloads.skybox, sizeof(*payloads.skybox));
        if (!skyboxFrame) {
            return false;
        }
        binding.skyboxFrame = *skyboxFrame;
    }
    return true;
}

const DeferredFrameResourceSet::Binding* DeferredFrameResourceSet::beginView(
    RenderSubmission&           submission,
    RenderViewRecordingContext& view,
    const SSAOFrameData*        ssao,
    const SkyboxFrameData*      skybox)
{
    if (!_render || !submission.isRecording()) {
        return nullptr;
    }
    if (!view.frameData) {
        YA_CORE_ERROR("Deferred beginView requires view frame data");
        return nullptr;
    }
    if (!beginViewBindingTable(_viewBindings, submission)) {
        return nullptr;
    }

    ViewResources* slot = _viewBindings.mutableNextView(submission.flightIndex());
    if (!slot) {
        YA_CORE_ERROR("Deferred beginView requires a recording submission on flight {}", submission.flightIndex());
        return nullptr;
    }

    const uint32_t viewSlot = _viewBindings.liveViewCount(submission.flightIndex());
    if (!prepareSkinning(submission, view)) {
        YA_CORE_ERROR("Deferred beginView failed to prepare scene-family skinning");
        return nullptr;
    }
    SceneFamilyResources* family = allocateSceneFamilyForView(submission, view);
    if (!family) {
        return nullptr;
    }
    slot->frame.skinningDescriptorSet = family->skinningDescriptorSet(_skinningDSL.get());
    slot->frame.skinningBuffer        = view.frameData->sceneResources.skinningBuffer;

    if (!ensureViewDescriptors(submission, slot->frame)) {
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
    if (!writeViewPayloads(submission, alignment, payloads, slot->frame)) {
        YA_CORE_ERROR("Deferred beginView failed to upload view payloads");
        return nullptr;
    }

    updateFrameAndLightDescriptorSet(slot->frame);
    if (payloads.ssao) {
        updateSSAODescriptorSet(slot->frame);
    }
    if (payloads.skybox) {
        updateSkyboxDescriptorSet(slot->frame);
    }

    if (!_viewBindings.commitNextView(submission.flightIndex())) {
        return nullptr;
    }

    view.viewSlot = viewSlot;
    return &_viewBindings.getView(submission.flightIndex(), viewSlot)->frame;
}

const DeferredFrameResourceSet::Binding* DeferredFrameResourceSet::getViewBinding(
    uint32_t flightIndex,
    uint32_t viewSlot) const
{
    const ViewResources* resources = _viewBindings.getView(flightIndex, viewSlot);
    return resources ? &resources->frame : nullptr;
}

const DeferredFrameResourceSet::ViewResources* DeferredFrameResourceSet::getViewResources(
    uint32_t flightIndex,
    uint32_t viewSlot) const
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

DeferredFrameResourceSet::ViewResources* DeferredFrameResourceSet::mutableViewResources(
    uint32_t flightIndex,
    uint32_t viewSlot)
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

uint32_t DeferredFrameResourceSet::liveViewCount(uint32_t flightIndex) const
{
    return _viewBindings.liveViewCount(flightIndex);
}

} // namespace ya
