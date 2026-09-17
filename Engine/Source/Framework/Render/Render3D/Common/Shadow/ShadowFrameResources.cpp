#include "ShadowFrameResources.h"

#include "Core/Log.h"
#include "RHI/Render.h"
#include "Render3D/Common/FrameResourceSubmission.h"
#include "Render3D/Common/SceneFamilyResources.h"
#include "Render3D/RenderFrameData.h"

#include <algorithm>
#include <vector>

namespace ya
{

void ShadowFrameResources::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "ShadowFrameResources requires a render backend");

    initSkinningLayout(render, "Shadow", "Shadow_Skinning_DSL", 1);

    _frameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Shadow_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0,
                          .descriptorType = EPipelineDescriptorType::UniformBuffer,
                          .descriptorCount = 1,
                          .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        });
}

void ShadowFrameResources::destroy()
{
    _viewBindings.clear();
    destroySkinningLayout();
    _frameDSL.reset();
}

bool ShadowFrameResources::prepareSkinning(
    RenderSubmission&                 submission,
    const RenderViewRecordingContext& view)
{
    SceneFamilyResources* family = allocateSceneFamilyForView(submission, view);
    IRenderResourceFactory* factory = submission.resourceFactory();
    if (!family || !factory) {
        return false;
    }
    return prepareSceneFamilySkinning(
        submission, *family, *factory, _render, _skinningDSL, _resourceTag);
}

bool ShadowFrameResources::ensureViewDescriptors(
    RenderSubmission& submission,
    Binding&          binding,
    uint32_t          directionalCount,
    uint32_t          pointFaceCount)
{
    for (uint32_t cascadeIndex = 0; cascadeIndex < directionalCount; ++cascadeIndex) {
        if (!binding.directionalFrameDS[cascadeIndex]) {
            binding.directionalFrameDS[cascadeIndex] = submission.allocateDescriptorSet(_frameDSL, 1);
        }
        if (!binding.directionalFrameDS[cascadeIndex]) {
            return false;
        }
    }
    for (uint32_t faceIndex = 0; faceIndex < pointFaceCount; ++faceIndex) {
        if (!binding.pointFaceDS[faceIndex]) {
            binding.pointFaceDS[faceIndex] = submission.allocateDescriptorSet(_frameDSL, 1);
        }
        if (!binding.pointFaceDS[faceIndex]) {
            return false;
        }
    }
    return true;
}

void ShadowFrameResources::updateViewDescriptors(
    const Binding& binding,
    uint32_t       directionalCount,
    uint32_t       pointFaceCount)
{
    std::vector<WriteDescriptorSet> writes;
    writes.reserve(directionalCount + pointFaceCount);
    for (uint32_t cascadeIndex = 0; cascadeIndex < directionalCount; ++cascadeIndex) {
        writes.push_back(IDescriptorSetHelper::genBufferWrite(
            binding.directionalFrameDS[cascadeIndex],
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.directionalFrames[cascadeIndex].descriptor()}));
    }
    for (uint32_t faceIndex = 0; faceIndex < pointFaceCount; ++faceIndex) {
        writes.push_back(IDescriptorSetHelper::genBufferWrite(
            binding.pointFaceDS[faceIndex],
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.pointFaces[faceIndex].descriptor()}));
    }
    if (!writes.empty()) {
        _render->getDescriptorHelper()->updateDescriptorSets(writes, {});
    }
}

ShadowFrameResources::ViewPayloads ShadowFrameResources::buildViewPayloads(
    const BasicShadowFramePayload& payload)
{
    ViewPayloads payloads{};
    payloads.directionalCount = payload.directionalCascadeCount();
    for (uint32_t cascadeIndex = 0; cascadeIndex < payloads.directionalCount; ++cascadeIndex) {
        payloads.directional[cascadeIndex] = DirectionalFrameData{
            .directionalLightMatrix = payload.frameData->directionalLight.cascadeViewProjections[cascadeIndex],
            .numPointLights         = 0,
            .hasDirectionalLight    = 1u,
        };
    }

    payloads.pointFaceCount = payload.pointEnabled()
        ? payload.pointLightCount * ShadowConstants::FACES_PER_POINT_LIGHT
        : 0u;
    for (uint32_t faceGlobalIndex = 0; faceGlobalIndex < payloads.pointFaceCount; ++faceGlobalIndex) {
        const uint32_t lightIndex = faceGlobalIndex / ShadowConstants::FACES_PER_POINT_LIGHT;
        const uint32_t faceIndex  = faceGlobalIndex % ShadowConstants::FACES_PER_POINT_LIGHT;
        payloads.pointFaces[faceGlobalIndex] = PointFaceData{
            .viewProj = payload.frameUBO.pointLights[lightIndex].matrix[faceIndex],
            .lightPos = payload.frameUBO.pointLights[lightIndex].pos,
            .farPlane = payload.frameUBO.pointLights[lightIndex].farPlane,
        };
    }
    return payloads;
}

bool ShadowFrameResources::writeViewPayloads(
    FrameUploadArena&   arena,
    uint32_t            flightIndex,
    uint32_t            alignment,
    const ViewPayloads& payloads,
    Binding&            binding)
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT || alignment == 0) {
        return false;
    }
    if (payloads.directionalCount > MAX_DIRECTIONAL_CASCADES ||
        payloads.pointFaceCount > ShadowConstants::POINT_SHADOW_FACE_COUNT) {
        return false;
    }

    for (uint32_t cascadeIndex = 0; cascadeIndex < payloads.directionalCount; ++cascadeIndex) {
        auto slice = writeUploadSlice(
            arena,
            flightIndex,
            alignment,
            &payloads.directional[cascadeIndex],
            sizeof(payloads.directional[cascadeIndex]));
        if (!slice) {
            return false;
        }
        binding.directionalFrames[cascadeIndex] = *slice;
    }
    for (uint32_t faceIndex = 0; faceIndex < payloads.pointFaceCount; ++faceIndex) {
        auto slice = writeUploadSlice(
            arena,
            flightIndex,
            alignment,
            &payloads.pointFaces[faceIndex],
            sizeof(payloads.pointFaces[faceIndex]));
        if (!slice) {
            return false;
        }
        binding.pointFaces[faceIndex] = *slice;
    }
    return true;
}

bool ShadowFrameResources::writeViewPayloads(
    RenderSubmission&   submission,
    uint32_t            alignment,
    const ViewPayloads& payloads,
    Binding&            binding)
{
    if (!submission.isRecording() || alignment == 0) {
        return false;
    }
    if (payloads.directionalCount > MAX_DIRECTIONAL_CASCADES ||
        payloads.pointFaceCount > ShadowConstants::POINT_SHADOW_FACE_COUNT) {
        return false;
    }

    for (uint32_t cascadeIndex = 0; cascadeIndex < payloads.directionalCount; ++cascadeIndex) {
        auto slice = writeUploadSlice(
            submission,
            alignment,
            &payloads.directional[cascadeIndex],
            sizeof(payloads.directional[cascadeIndex]));
        if (!slice) {
            return false;
        }
        binding.directionalFrames[cascadeIndex] = *slice;
    }
    for (uint32_t faceIndex = 0; faceIndex < payloads.pointFaceCount; ++faceIndex) {
        auto slice = writeUploadSlice(
            submission,
            alignment,
            &payloads.pointFaces[faceIndex],
            sizeof(payloads.pointFaces[faceIndex]));
        if (!slice) {
            return false;
        }
        binding.pointFaces[faceIndex] = *slice;
    }
    return true;
}

const ShadowFrameResources::Binding* ShadowFrameResources::beginView(
    RenderSubmission&              submission,
    RenderViewRecordingContext&    view,
    const BasicShadowFramePayload& payload)
{
    if (!_render || !submission.isRecording()) {
        return nullptr;
    }
    if (!view.frameData || !payload.frameData) {
        YA_CORE_ERROR("Shadow beginView requires view frame data");
        return nullptr;
    }
    if (!beginViewBindingTable(_viewBindings, submission)) {
        return nullptr;
    }

    Binding* slot = _viewBindings.mutableNextView(submission.flightIndex());
    if (!slot) {
        YA_CORE_ERROR("Shadow beginView requires a recording submission on flight {}", submission.flightIndex());
        return nullptr;
    }

    const uint32_t viewSlot = _viewBindings.liveViewCount(submission.flightIndex());
    if (!prepareSkinning(submission, view)) {
        YA_CORE_ERROR("Shadow beginView failed to prepare scene-family skinning");
        return nullptr;
    }
    SceneFamilyResources* family = allocateSceneFamilyForView(submission, view);
    if (!family) {
        return nullptr;
    }
    slot->skinningDS     = family->skinningDescriptorSet(_skinningDSL.get());
    slot->skinningBuffer = family->gpu().skinningBuffer;

    const ViewPayloads payloads = buildViewPayloads(payload);
    if (!ensureViewDescriptors(submission, *slot, payloads.directionalCount, payloads.pointFaceCount)) {
        YA_CORE_ERROR("Shadow beginView failed to allocate view descriptor sets");
        return nullptr;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    if (!writeViewPayloads(submission, alignment, payloads, *slot)) {
        YA_CORE_ERROR("Shadow beginView failed to upload view payloads");
        return nullptr;
    }

    updateViewDescriptors(*slot, payloads.directionalCount, payloads.pointFaceCount);

    if (!_viewBindings.commitNextView(submission.flightIndex())) {
        return nullptr;
    }

    view.viewSlot = viewSlot;
    return _viewBindings.getView(submission.flightIndex(), viewSlot);
}

const ShadowFrameResources::Binding* ShadowFrameResources::getViewBinding(
    uint32_t flightIndex,
    uint32_t viewSlot) const
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

ShadowFrameResources::Binding* ShadowFrameResources::mutableViewBinding(
    uint32_t flightIndex,
    uint32_t viewSlot)
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

uint32_t ShadowFrameResources::liveViewCount(uint32_t flightIndex) const
{
    return _viewBindings.liveViewCount(flightIndex);
}

} // namespace ya
