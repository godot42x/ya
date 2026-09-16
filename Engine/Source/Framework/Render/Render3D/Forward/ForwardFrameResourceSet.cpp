#include "ForwardFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Render.h"
#include "Render3D/Common/FrameResourceSubmission.h"

#include <algorithm>

namespace ya
{

void ForwardFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "ForwardFrameResourceSet requires a render backend");

    initSkinnedUploadArena(render, "Forward", "Forward_Skinning_DSL", 5, "Forward.FrameUpload");

    _pbrFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdPBR_Frame_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment},
            },
        });
    _pbrFrameSets.init(_render, "FwdPBR_Frame_DSP", 2);

    _phongFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdPhong_Frame_DSL",
            .set      = 0,
            .bindings = {
                {.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                {.binding = 1, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
                {.binding = 2, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment},
            },
        });
    _phongFrameSets.init(_render, "FwdPhong_Frame_DSP", 3);

    _unlitFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdUnlit_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        });
    _unlitFrameSets.init(_render, "FwdUnlit_Frame_DSP", 1);

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_PerFrame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    _skyboxFrameSets.init(_render, "FwdSkybox_DSP", 1);

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "ForwardFrameResourceSet failed to create initial skinning resources");
}

void ForwardFrameResourceSet::destroy()
{
    _viewBindings.clear();
    _skinningBindings = {};
    destroySkinnedUploadArena();
    _pbrFrameSets.destroy();
    _pbrFrameDSL.reset();
    _phongFrameSets.destroy();
    _phongFrameDSL.reset();
    _unlitFrameSets.destroy();
    _unlitFrameDSL.reset();
    _skyboxFrameSets.destroy();
    _skyboxFrameDSL.reset();
}

bool ForwardFrameResourceSet::ensureViewDescriptors(Binding& binding)
{
    if (binding.pbrFrameDescriptorSet && binding.phongFrameDescriptorSet &&
        binding.unlitFrameDescriptorSet && binding.skyboxFrameDescriptorSet) {
        return true;
    }

    if (!binding.pbrFrameDescriptorSet) {
        binding.pbrFrameDescriptorSet = _pbrFrameSets.allocate(_pbrFrameDSL);
    }
    if (!binding.phongFrameDescriptorSet) {
        binding.phongFrameDescriptorSet = _phongFrameSets.allocate(_phongFrameDSL);
    }
    if (!binding.unlitFrameDescriptorSet) {
        binding.unlitFrameDescriptorSet = _unlitFrameSets.allocate(_unlitFrameDSL);
    }
    if (!binding.skyboxFrameDescriptorSet) {
        binding.skyboxFrameDescriptorSet = _skyboxFrameSets.allocate(_skyboxFrameDSL);
    }

    return binding.pbrFrameDescriptorSet && binding.phongFrameDescriptorSet &&
           binding.unlitFrameDescriptorSet && binding.skyboxFrameDescriptorSet;
}

bool ForwardFrameResourceSet::beginSubmission(const RenderSubmissionContext& submission)
{
    if (!_render) {
        return false;
    }
    return beginFrameResourceSubmission(_uploadArena.get(), _viewBindings, submission);
}

bool ForwardFrameResourceSet::writeViewPayloads(
    FrameUploadArena& arena,
    uint32_t          flightIndex,
    uint32_t          alignment,
    const FramePayloads& payloads,
    Binding&          binding)
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT || alignment == 0) {
        return false;
    }

    auto pbrFrame = writeUploadSlice(arena, flightIndex, alignment, &payloads.pbrFrame, sizeof(payloads.pbrFrame));
    auto pbrLight = writeUploadSlice(arena, flightIndex, alignment, &payloads.pbrLight, sizeof(payloads.pbrLight));
    auto phongFrame = writeUploadSlice(arena, flightIndex, alignment, &payloads.phongFrame, sizeof(payloads.phongFrame));
    auto phongLight = writeUploadSlice(arena, flightIndex, alignment, &payloads.phongLight, sizeof(payloads.phongLight));
    auto phongDebug = writeUploadSlice(arena, flightIndex, alignment, &payloads.phongDebug, sizeof(payloads.phongDebug));
    auto unlitFrame = writeUploadSlice(arena, flightIndex, alignment, &payloads.unlitFrame, sizeof(payloads.unlitFrame));
    auto skyboxFrame = writeUploadSlice(arena, flightIndex, alignment, &payloads.skyboxFrame, sizeof(payloads.skyboxFrame));
    if (!pbrFrame || !pbrLight || !phongFrame || !phongLight || !phongDebug || !unlitFrame || !skyboxFrame) {
        return false;
    }

    binding.pbrFrame    = *pbrFrame;
    binding.pbrLight    = *pbrLight;
    binding.phongFrame  = *phongFrame;
    binding.phongLight  = *phongLight;
    binding.phongDebug  = *phongDebug;
    binding.unlitFrame  = *unlitFrame;
    binding.skyboxFrame = *skyboxFrame;
    return true;
}

const ForwardFrameResourceSet::Binding* ForwardFrameResourceSet::beginView(
    const RenderSubmissionContext& submission,
    RenderViewRecordingContext&    view,
    const FramePayloads&           payloads)
{
    if (!_render || !_uploadArena || submission.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return nullptr;
    }

    Binding* slot = _viewBindings.mutableNextView(submission.flightIndex);
    if (!slot) {
        YA_CORE_ERROR("Forward beginView requires beginSubmission on flight {}", submission.flightIndex);
        return nullptr;
    }

    const uint32_t viewSlot = _viewBindings.liveViewCount(submission.flightIndex);
    const SkinningBinding& skinning = _skinningBindings[submission.flightIndex];
    slot->skinningDescriptorSet = skinning.skinningDescriptorSet;
    slot->skinningBuffer        = skinning.skinningBuffer;

    if (!ensureViewDescriptors(*slot)) {
        YA_CORE_ERROR("Forward beginView failed to allocate view descriptor sets");
        return nullptr;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    if (!writeViewPayloads(*_uploadArena, submission.flightIndex, alignment, payloads, *slot)) {
        YA_CORE_ERROR("Forward beginView failed to upload view payloads");
        return nullptr;
    }

    updatePBRFrameDescriptorSet(*slot);
    updatePhongFrameDescriptorSet(*slot);
    updateUnlitFrameDescriptorSet(*slot);
    updateSkyboxFrameDescriptorSet(*slot);

    if (!_viewBindings.commitNextView(submission.flightIndex)) {
        return nullptr;
    }

    view.viewSlot = viewSlot;
    return _viewBindings.getView(submission.flightIndex, viewSlot);
}

const ForwardFrameResourceSet::Binding* ForwardFrameResourceSet::getViewBinding(
    uint32_t flightIndex,
    uint32_t viewSlot) const
{
    return _viewBindings.getView(flightIndex, viewSlot);
}

uint32_t ForwardFrameResourceSet::liveViewCount(uint32_t flightIndex) const
{
    return _viewBindings.liveViewCount(flightIndex);
}

void ForwardFrameResourceSet::updatePBRFrameDescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.pbrFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.pbrFrame.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            binding.pbrFrameDescriptorSet,
            1,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.pbrLight.descriptor()}),
    });
}

void ForwardFrameResourceSet::updatePhongFrameDescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.phongFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.phongFrame.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            binding.phongFrameDescriptorSet,
            1,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.phongLight.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            binding.phongFrameDescriptorSet,
            2,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.phongDebug.descriptor()}),
    });
}

void ForwardFrameResourceSet::updateUnlitFrameDescriptorSet(const Binding& binding)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.unlitFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.unlitFrame.descriptor()}),
    });
}

void ForwardFrameResourceSet::updateSkyboxFrameDescriptorSet(const Binding& binding)
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

} // namespace ya
