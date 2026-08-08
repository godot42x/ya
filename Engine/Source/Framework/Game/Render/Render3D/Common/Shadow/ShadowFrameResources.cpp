#include "ShadowFrameResources.h"

#include "Foundation/Core/Log.h"
#include "Foundation/Core/Common/DeferredDeletionQueue.h"
#include "Foundation/RHI/Render.h"
#include "Framework/Game/Render/Render3D/RenderFrameData.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ya
{

namespace
{

bool checkedAdd(uint64_t lhs, uint64_t rhs, uint64_t& result)
{
    if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

uint64_t alignUp(uint64_t value, uint32_t alignment)
{
    const uint64_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

} // namespace

void ShadowFrameResources::init(IRender* render)
{
    _render = render;
    YA_CORE_ASSERT(_render != nullptr, "ShadowFrameResources requires a render backend");

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
    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Shadow_Skinning_DSL",
            .set      = 1,
            .bindings = {{.binding = 0,
                          .descriptorType = EPipelineDescriptorType::StorageBuffer,
                          .descriptorCount = 1,
                          .stageFlags = EShaderStage::Vertex}},
        });

    const uint32_t directionalCount = MAX_FLIGHTS_IN_FLIGHT * MAX_DIRECTIONAL_CASCADES;
    const uint32_t pointFaceCount   = MAX_FLIGHTS_IN_FLIGHT * ShadowConstants::POINT_SHADOW_FACE_COUNT;
    _descriptorPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Shadow_Frame_DSP",
            .maxSets   = directionalCount + pointFaceCount + MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {
                {.type = EPipelineDescriptorType::UniformBuffer,
                 .descriptorCount = directionalCount + pointFaceCount},
                {.type = EPipelineDescriptorType::StorageBuffer,
                 .descriptorCount = MAX_FLIGHTS_IN_FLIGHT},
            },
        });

    _uploadArena = std::make_unique<FrameUploadArena>(
        *_render->getResourceFactory(),
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer | EBufferUsage::StorageBuffer,
        "ShadowFrameUploadArena");

    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        auto& binding = _bindings[flightIndex];
        for (auto& descriptorSet : binding.directionalFrameDS) {
            descriptorSet = _descriptorPool->allocateDescriptorSets(_frameDSL);
        }
        for (auto& descriptorSet : binding.pointFaceDS) {
            descriptorSet = _descriptorPool->allocateDescriptorSets(_frameDSL);
        }
        binding.skinningDS = _descriptorPool->allocateDescriptorSets(_skinningDSL);
    }

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "ShadowFrameResources failed to create initial skinning resources");
}

void ShadowFrameResources::destroy()
{
    _bindings = {};
    _uploadArena.reset();
    _descriptorPool.reset();
    _skinningDSL.reset();
    _frameDSL.reset();
    _skinningCapacity = 0;
    _render = nullptr;
}

std::optional<uint32_t> ShadowFrameResources::calculateSkinningCapacity(
    uint32_t currentCapacity,
    uint32_t paletteCount)
{
    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        return std::nullopt;
    }

    uint32_t nextCapacity = currentCapacity == 0 ? 16u : currentCapacity;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }
    return nextCapacity;
}

bool ShadowFrameResources::ensureSkinningCapacity(uint32_t paletteCount)
{
    if (_descriptorPool && std::max(1u, paletteCount) <= _skinningCapacity) {
        return true;
    }

    const auto nextCapacity = calculateSkinningCapacity(_skinningCapacity, paletteCount);
    if (!nextCapacity.has_value()) {
        YA_CORE_ERROR("Shadow skinning palette count {} exceeds buffer size limit", paletteCount);
        return false;
    }

    const uint32_t bufferSize = *nextCapacity * sizeof(RenderSkinningPalette);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> nextBuffers{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        nextBuffers[flightIndex] = _render->getResourceFactory()->createBuffer(
            BufferCreateInfo{
                .label       = std::format("Shadow_Skinning_SSBO_{}", flightIndex),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        if (!nextBuffers[flightIndex]) {
            YA_CORE_ERROR("ShadowFrameResources failed to create skinning buffer for flight {}", flightIndex);
            return false;
        }
    }

    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> oldBuffers{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        oldBuffers[flightIndex] = std::move(_bindings[flightIndex].skinningBuffer);
        _bindings[flightIndex].skinningBuffer = std::move(nextBuffers[flightIndex]);
        _render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::genSingleBufferWrite(
                _bindings[flightIndex].skinningDS,
                0,
                EPipelineDescriptorType::StorageBuffer,
                _bindings[flightIndex].skinningBuffer.get())},
            {});
    }
    _skinningCapacity = *nextCapacity;

    if (DeferredDeletionQueue::get().isInitialized()) {
        for (auto& oldBuffer : oldBuffers) {
            DeferredDeletionQueue::get().retireResource(std::move(oldBuffer));
        }
    }
    return true;
}

bool ShadowFrameResources::prepare(const BasicShadowFramePayload& payload)
{
    if (!_render || !_uploadArena || !payload.frameData || payload.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }
    if (!_uploadArena->beginFlight(payload.flightIndex) ||
        !ensureSkinningCapacity(static_cast<uint32_t>(payload.frameData->skinningPalettes.size()))) {
        return false;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    const uint32_t directionalCount = payload.directionalCascadeCount();
    const uint32_t pointFaceCount = payload.pointEnabled()
        ? payload.pointLightCount * ShadowConstants::FACES_PER_POINT_LIGHT
        : 0u;
    const uint64_t sliceCount = static_cast<uint64_t>(directionalCount) + pointFaceCount;
    uint64_t reservationSize = 0;
    if (sliceCount > 0) {
        if (!checkedAdd(
                static_cast<uint64_t>(sliceCount) * (alignment - 1u),
                static_cast<uint64_t>(directionalCount) * sizeof(DirectionalFrameData) +
                    static_cast<uint64_t>(pointFaceCount) * sizeof(PointFaceData),
                reservationSize) ||
            reservationSize > std::numeric_limits<uint32_t>::max()) {
            YA_CORE_ERROR("Shadow frame upload reservation exceeds 32-bit buffer size");
            return false;
        }
    }

    Binding next = _bindings[payload.flightIndex];
    next.directionalFrames.fill({});
    next.pointFaces.fill({});

    std::optional<FrameUploadArena::Allocation> reservation;
    if (reservationSize > 0) {
        reservation = _uploadArena->allocate(
            payload.flightIndex,
            static_cast<uint32_t>(reservationSize),
            alignment);
        if (!reservation.has_value()) {
            return false;
        }
    }

    uint64_t cursor = reservation ? reservation->offset : 0;
    auto allocateSlice = [&](uint32_t size) -> FrameUploadArena::Allocation {
        cursor = alignUp(cursor, alignment);
        FrameUploadArena::Allocation result{
            .buffer = reservation ? reservation->buffer : nullptr,
            .offset = cursor,
            .size   = size,
        };
        cursor += size;
        return result;
    };

    for (uint32_t cascadeIndex = 0; cascadeIndex < directionalCount; ++cascadeIndex) {
        auto slice = allocateSlice(sizeof(DirectionalFrameData));
        DirectionalFrameData frameData{
            .directionalLightMatrix = payload.frameData->directionalLight.cascadeViewProjections[cascadeIndex],
            .numPointLights         = 0,
            .hasDirectionalLight    = 1u,
        };
        if (!slice.write(&frameData, sizeof(frameData))) {
            return false;
        }
        next.directionalFrames[cascadeIndex] = slice;
    }

    for (uint32_t faceGlobalIndex = 0; faceGlobalIndex < pointFaceCount; ++faceGlobalIndex) {
        const uint32_t lightIndex = faceGlobalIndex / ShadowConstants::FACES_PER_POINT_LIGHT;
        const uint32_t faceIndex = faceGlobalIndex % ShadowConstants::FACES_PER_POINT_LIGHT;
        auto slice = allocateSlice(sizeof(PointFaceData));
        PointFaceData faceData{
            .viewProj  = payload.frameUBO.pointLights[lightIndex].matrix[faceIndex],
            .lightPos  = payload.frameUBO.pointLights[lightIndex].pos,
            .farPlane  = payload.frameUBO.pointLights[lightIndex].farPlane,
        };
        if (!slice.write(&faceData, sizeof(faceData))) {
            return false;
        }
        next.pointFaces[faceGlobalIndex] = slice;
    }

    const auto& palettes = payload.frameData->skinningPalettes;
    auto&       skinning = next.skinningBuffer;
    if (!palettes.empty()) {
        const uint64_t bytes = static_cast<uint64_t>(palettes.size()) * sizeof(RenderSkinningPalette);
        if (bytes > std::numeric_limits<uint32_t>::max() ||
            !skinning->writeData(palettes.data(), static_cast<uint32_t>(bytes), 0) ||
            !skinning->flush(static_cast<uint32_t>(bytes), 0)) {
            return false;
        }
    }

    std::vector<WriteDescriptorSet> writes;
    writes.reserve(directionalCount + pointFaceCount);
    for (uint32_t cascadeIndex = 0; cascadeIndex < directionalCount; ++cascadeIndex) {
        writes.push_back(IDescriptorSetHelper::genBufferWrite(
            next.directionalFrameDS[cascadeIndex], 0, 0,
            EPipelineDescriptorType::UniformBuffer,
            {next.directionalFrames[cascadeIndex].descriptor()}));
    }
    for (uint32_t faceGlobalIndex = 0; faceGlobalIndex < pointFaceCount; ++faceGlobalIndex) {
        writes.push_back(IDescriptorSetHelper::genBufferWrite(
            next.pointFaceDS[faceGlobalIndex], 0, 0,
            EPipelineDescriptorType::UniformBuffer,
            {next.pointFaces[faceGlobalIndex].descriptor()}));
    }
    if (!writes.empty()) {
        _render->getDescriptorHelper()->updateDescriptorSets(writes, {});
    }
    _bindings[payload.flightIndex] = std::move(next);
    return true;
}

const ShadowFrameResources::Binding& ShadowFrameResources::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "ShadowFrameResources invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
