#include "ForwardFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <algorithm>
#include <format>
#include <limits>

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
    _pbrFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "FwdPBR_Frame_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2}},
        });

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
    _phongFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "FwdPhong_Frame_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 3}},
        });

    _unlitFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdUnlit_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex | EShaderStage::Fragment}},
        });
    _unlitFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "FwdUnlit_Frame_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "FwdSkybox_PerFrame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    _skyboxFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "FwdSkybox_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        _bindings[flightIndex] = Binding{
            .skinningDescriptorSet   = DescriptorSetHandle{},
            .pbrFrameDescriptorSet   = _pbrFrameDSP->allocateDescriptorSets(_pbrFrameDSL),
            .phongFrameDescriptorSet = _phongFrameDSP->allocateDescriptorSets(_phongFrameDSL),
            .unlitFrameDescriptorSet = _unlitFrameDSP->allocateDescriptorSets(_unlitFrameDSL),
            .skyboxFrameDescriptorSet = _skyboxFrameDSP->allocateDescriptorSets(_skyboxFrameDSL),
        };
    }

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "ForwardFrameResourceSet failed to create initial skinning resources");
}

void ForwardFrameResourceSet::destroy()
{
    _bindings = {};
    destroySkinnedUploadArena();
    _pbrFrameDSP.reset();
    _pbrFrameDSL.reset();
    _phongFrameDSP.reset();
    _phongFrameDSL.reset();
    _unlitFrameDSP.reset();
    _unlitFrameDSL.reset();
    _skyboxFrameDSP.reset();
    _skyboxFrameDSL.reset();
}

bool ForwardFrameResourceSet::prepareFramePayloads(
    const RenderStageContext& ctx,
    const FramePayloads& payloads)
{
    if (!_render || !_uploadArena || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }
    if (!_uploadArena->beginFlight(ctx.flightIndex)) {
        return false;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    const uint32_t flight    = ctx.flightIndex;

    auto writeSlice = [&](const void* data, uint32_t size) -> std::optional<FrameUploadArena::Allocation>
    {
        auto slice = _uploadArena->allocate(flight, size, alignment);
        if (!slice.has_value() || !slice->write(data, size)) {
            return std::nullopt;
        }
        return slice;
    };

    auto pbrFrame = writeSlice(&payloads.pbrFrame, sizeof(payloads.pbrFrame));
    auto pbrLight = writeSlice(&payloads.pbrLight, sizeof(payloads.pbrLight));
    if (pbrFrame && pbrLight) {
        updatePBRFrameDescriptorSet(flight, *pbrFrame, *pbrLight);
    }

    auto phongFrame = writeSlice(&payloads.phongFrame, sizeof(payloads.phongFrame));
    auto phongLight = writeSlice(&payloads.phongLight, sizeof(payloads.phongLight));
    auto phongDebug = writeSlice(&payloads.phongDebug, sizeof(payloads.phongDebug));
    if (phongFrame && phongLight && phongDebug) {
        updatePhongFrameDescriptorSet(flight, *phongFrame, *phongLight, *phongDebug);
    }

    auto unlitFrame = writeSlice(&payloads.unlitFrame, sizeof(payloads.unlitFrame));
    if (unlitFrame) {
        updateUnlitFrameDescriptorSet(flight, *unlitFrame);
    }

    auto skyboxFrame = writeSlice(&payloads.skyboxFrame, sizeof(payloads.skyboxFrame));
    if (skyboxFrame) {
        updateSkyboxFrameDescriptorSet(flight, *skyboxFrame);
    }

    return true;
}

void ForwardFrameResourceSet::updatePBRFrameDescriptorSet(
    uint32_t flightIndex,
    const FrameUploadArena::Allocation& frame,
    const FrameUploadArena::Allocation& light)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].pbrFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {frame.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].pbrFrameDescriptorSet,
            1,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {light.descriptor()}),
    });
}

void ForwardFrameResourceSet::updatePhongFrameDescriptorSet(
    uint32_t flightIndex,
    const FrameUploadArena::Allocation& frame,
    const FrameUploadArena::Allocation& light,
    const FrameUploadArena::Allocation& debug)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].phongFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {frame.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].phongFrameDescriptorSet,
            1,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {light.descriptor()}),
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].phongFrameDescriptorSet,
            2,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {debug.descriptor()}),
    });
}

void ForwardFrameResourceSet::updateUnlitFrameDescriptorSet(
    uint32_t flightIndex,
    const FrameUploadArena::Allocation& frame)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].unlitFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {frame.descriptor()}),
    });
}

void ForwardFrameResourceSet::updateSkyboxFrameDescriptorSet(
    uint32_t flightIndex,
    const FrameUploadArena::Allocation& frame)
{
    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            _bindings[flightIndex].skyboxFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {frame.descriptor()}),
    });
}

const ForwardFrameResourceSet::Binding& ForwardFrameResourceSet::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "ForwardFrameResourceSet invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
