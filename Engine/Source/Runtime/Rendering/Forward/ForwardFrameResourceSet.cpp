#include "ForwardFrameResourceSet.h"

#include "Core/Log.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/RenderResourceFactory.h"
#include "Render/Render.h"
#include "Resource/DeferredDeletionQueue.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ya
{

void ForwardFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "ForwardFrameResourceSet requires a render backend");

    _render = render;

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Forward_Skinning_DSL",
            .set      = 5,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });
    YA_CORE_ASSERT(_skinningDSL != nullptr, "ForwardFrameResourceSet failed to create skinning descriptor layout");

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

    _uploadArena = std::make_unique<FrameUploadArena>(
        *render->getResourceFactory(),
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer,
        "Forward.FrameUpload");

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
    _uploadArena.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _pbrFrameDSP.reset();
    _pbrFrameDSL.reset();
    _phongFrameDSP.reset();
    _phongFrameDSL.reset();
    _unlitFrameDSP.reset();
    _unlitFrameDSL.reset();
    _skyboxFrameDSP.reset();
    _skyboxFrameDSL.reset();
    _skinningCapacity = 0;
    _render = nullptr;
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

bool ForwardFrameResourceSet::ensureSkinningCapacity(uint32_t paletteCount)
{
    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        YA_CORE_ERROR("Forward skinning palette count {} exceeds buffer size limit", paletteCount);
        return false;
    }

    if (_skinningDSP && requiredCount <= _skinningCapacity) {
        return true;
    }

    uint32_t nextCapacity = _skinningCapacity == 0 ? 16u : _skinningCapacity;
    if (nextCapacity > maxPaletteCount) {
        YA_CORE_ERROR("Forward skinning capacity {} exceeds buffer size limit", nextCapacity);
        return false;
    }
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    auto nextDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Forward_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });
    if (!nextDSP) {
        YA_CORE_ERROR("ForwardFrameResourceSet failed to create skinning descriptor pool");
        return false;
    }

    const uint32_t bufferSize = nextCapacity * sizeof(RenderSkinningPalette);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> nextBuffers{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> nextDescriptorSets{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        nextBuffers[flightIndex] = _render->getResourceFactory()->createBuffer(
            BufferCreateInfo{
                .label       = std::format("Forward_Skinning_SSBO_{}", flightIndex),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        if (!nextBuffers[flightIndex]) {
            YA_CORE_ERROR("ForwardFrameResourceSet failed to create skinning buffer for flight {}", flightIndex);
            return false;
        }

        nextDescriptorSets[flightIndex] = nextDSP->allocateDescriptorSets(_skinningDSL);
        if (!nextDescriptorSets[flightIndex]) {
            YA_CORE_ERROR("ForwardFrameResourceSet failed to allocate skinning descriptor set for flight {}", flightIndex);
            return false;
        }

        _render->getDescriptorHelper()->updateDescriptorSets(
            {IDescriptorSetHelper::genSingleBufferWrite(
                nextDescriptorSets[flightIndex],
                0,
                EPipelineDescriptorType::StorageBuffer,
                nextBuffers[flightIndex].get())},
            {});
    }

    auto oldDSP = std::move(_skinningDSP);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> oldBuffers{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        oldBuffers[flightIndex] = std::move(_bindings[flightIndex].skinningBuffer);
    }

    _skinningDSP      = std::move(nextDSP);
    _skinningCapacity = nextCapacity;
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        _bindings[flightIndex].skinningDescriptorSet = nextDescriptorSets[flightIndex];
        _bindings[flightIndex].skinningBuffer        = std::move(nextBuffers[flightIndex]);
    }

    if (!DeferredDeletionQueue::get().isInitialized()) {
        return true;
    }

    DeferredDeletionQueue::get().retireResource(std::move(oldDSP));
    for (auto& oldBuffer : oldBuffers) {
        DeferredDeletionQueue::get().retireResource(std::move(oldBuffer));
    }
    return true;
}

bool ForwardFrameResourceSet::prepareSkinning(const RenderStageContext& ctx)
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "Forward skinning prepare requires frame data");
    const auto& palettes = ctx.frameData->skinningPalettes;
    if (palettes.size() > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Forward skinning palette count exceeds uint32 range");
        return false;
    }
    if (!ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()))) {
        return false;
    }
    if (palettes.empty()) {
        return true;
    }

    auto& buffer = _bindings[ctx.flightIndex].skinningBuffer;
    YA_CORE_ASSERT(buffer != nullptr, "Forward skinning buffer is missing for flight {}", ctx.flightIndex);
    const uint32_t byteCount = static_cast<uint32_t>(palettes.size() * sizeof(RenderSkinningPalette));
    return buffer->writeData(palettes.data(), byteCount, 0) && buffer->flush(byteCount, 0);
}

const ForwardFrameResourceSet::Binding& ForwardFrameResourceSet::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "ForwardFrameResourceSet invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
