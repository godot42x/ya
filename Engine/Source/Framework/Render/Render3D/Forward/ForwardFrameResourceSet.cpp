#include "ForwardFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Render.h"

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
    _pbrFrameDSPs.push_back(createViewDescriptorPool("FwdPBR_Frame_DSP", kViewDescriptorChunk * 2));

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
    _phongFrameDSPs.push_back(createViewDescriptorPool("FwdPhong_Frame_DSP", kViewDescriptorChunk * 3));

    _unlitFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdUnlit_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        });
    _unlitFrameDSPs.push_back(createViewDescriptorPool("FwdUnlit_Frame_DSP", kViewDescriptorChunk));

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_PerFrame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    _skyboxFrameDSPs.push_back(createViewDescriptorPool("FwdSkybox_DSP", kViewDescriptorChunk));

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "ForwardFrameResourceSet failed to create initial skinning resources");
}

void ForwardFrameResourceSet::destroy()
{
    _viewBindings.clear();
    _skinningBindings = {};
    _pbrAllocatedSets = 0;
    _phongAllocatedSets = 0;
    _unlitAllocatedSets = 0;
    _skyboxAllocatedSets = 0;
    destroySkinnedUploadArena();
    _pbrFrameDSPs.clear();
    _pbrFrameDSL.reset();
    _phongFrameDSPs.clear();
    _phongFrameDSL.reset();
    _unlitFrameDSPs.clear();
    _unlitFrameDSL.reset();
    _skyboxFrameDSPs.clear();
    _skyboxFrameDSL.reset();
}

stdptr<IDescriptorPool> ForwardFrameResourceSet::createViewDescriptorPool(
    const char* label,
    uint32_t    descriptorCount)
{
    return IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = label,
            .maxSets   = kViewDescriptorChunk,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = descriptorCount}},
        });
}

DescriptorSetHandle ForwardFrameResourceSet::allocateViewSet(
    std::vector<stdptr<IDescriptorPool>>& pools,
    uint32_t&                             allocatedSets,
    const stdptr<IDescriptorSetLayout>&   layout,
    const char*                           poolLabel,
    uint32_t                              descriptorsPerSet)
{
    const uint32_t capacity = kViewDescriptorChunk * static_cast<uint32_t>(pools.size());
    if (pools.empty() || allocatedSets >= capacity) {
        auto nextPool = createViewDescriptorPool(poolLabel, kViewDescriptorChunk * descriptorsPerSet);
        if (!nextPool) {
            YA_CORE_ERROR("ForwardFrameResourceSet failed to grow {} descriptor pool", poolLabel);
            return {};
        }
        pools.push_back(std::move(nextPool));
    }

    DescriptorSetHandle set = pools.back()->allocateDescriptorSets(layout);
    if (set) {
        ++allocatedSets;
    }
    return set;
}

bool ForwardFrameResourceSet::ensureViewDescriptors(Binding& binding)
{
    if (binding.pbrFrameDescriptorSet && binding.phongFrameDescriptorSet &&
        binding.unlitFrameDescriptorSet && binding.skyboxFrameDescriptorSet) {
        return true;
    }

    if (!binding.pbrFrameDescriptorSet) {
        binding.pbrFrameDescriptorSet = allocateViewSet(_pbrFrameDSPs, _pbrAllocatedSets, _pbrFrameDSL, "FwdPBR_Frame_DSP", 2);
    }
    if (!binding.phongFrameDescriptorSet) {
        binding.phongFrameDescriptorSet = allocateViewSet(_phongFrameDSPs, _phongAllocatedSets, _phongFrameDSL, "FwdPhong_Frame_DSP", 3);
    }
    if (!binding.unlitFrameDescriptorSet) {
        binding.unlitFrameDescriptorSet = allocateViewSet(_unlitFrameDSPs, _unlitAllocatedSets, _unlitFrameDSL, "FwdUnlit_Frame_DSP", 1);
    }
    if (!binding.skyboxFrameDescriptorSet) {
        binding.skyboxFrameDescriptorSet = allocateViewSet(_skyboxFrameDSPs, _skyboxAllocatedSets, _skyboxFrameDSL, "FwdSkybox_DSP", 1);
    }

    return binding.pbrFrameDescriptorSet && binding.phongFrameDescriptorSet &&
           binding.unlitFrameDescriptorSet && binding.skyboxFrameDescriptorSet;
}

bool ForwardFrameResourceSet::beginSubmission(const RenderSubmissionContext& submission)
{
    if (!_render || !_uploadArena || submission.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }
    if (!_uploadArena->beginFlight(submission.flightIndex, submission.frameToken)) {
        return false;
    }
    return _viewBindings.beginSubmission(submission.flightIndex, submission.frameToken);
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

    auto writeSlice = [&](const void* data, uint32_t size) -> std::optional<FrameUploadArena::Allocation>
    {
        auto slice = arena.allocate(flightIndex, size, alignment);
        if (!slice.has_value() || !slice->write(data, size)) {
            return std::nullopt;
        }
        return slice;
    };

    auto pbrFrame = writeSlice(&payloads.pbrFrame, sizeof(payloads.pbrFrame));
    auto pbrLight = writeSlice(&payloads.pbrLight, sizeof(payloads.pbrLight));
    auto phongFrame = writeSlice(&payloads.phongFrame, sizeof(payloads.phongFrame));
    auto phongLight = writeSlice(&payloads.phongLight, sizeof(payloads.phongLight));
    auto phongDebug = writeSlice(&payloads.phongDebug, sizeof(payloads.phongDebug));
    auto unlitFrame = writeSlice(&payloads.unlitFrame, sizeof(payloads.unlitFrame));
    auto skyboxFrame = writeSlice(&payloads.skyboxFrame, sizeof(payloads.skyboxFrame));
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
