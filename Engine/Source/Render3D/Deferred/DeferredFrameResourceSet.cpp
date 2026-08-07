#include "DeferredFrameResourceSet.h"

#include "Core/Log.h"
#include "RHI/Render.h"
#include "Core/Common/DeferredDeletionQueue.h"

#include <algorithm>
#include <format>
#include <limits>

namespace ya
{

void DeferredFrameResourceSet::init(IRender* render)
{
    destroy();
    YA_CORE_ASSERT(render != nullptr, "DeferredFrameResourceSet requires a render backend");
    YA_CORE_ASSERT(render->getResourceFactory() != nullptr, "DeferredFrameResourceSet requires a resource factory");

    _render = render;
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

    _frameAndLightDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_Frame_And_Light_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT * 2}},
        });

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skinning_DSL",
            .set      = 3,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::StorageBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });

    _ssaoFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_SSAO_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
        });

    _ssaoFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_SSAO_Frame_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    _skyboxFrameDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "Deferred_Skybox_Frame_DSL",
            .set      = 0,
            .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Vertex}},
        });

    _skyboxFrameDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_Skybox_Frame_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::UniformBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });

    _uploadArena = std::make_unique<FrameUploadArena>(
        *render->getResourceFactory(),
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer,
        "Deferred.FrameUpload");

    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        _bindings[flightIndex] = Binding{
            .frameAndLightDescriptorSet = _frameAndLightDSP->allocateDescriptorSets(_frameAndLightDSL),
            .ssaoFrameDescriptorSet     = _ssaoFrameDSP->allocateDescriptorSets(_ssaoFrameDSL),
            .skyboxFrameDescriptorSet   = _skyboxFrameDSP->allocateDescriptorSets(_skyboxFrameDSL),
        };
    }

    YA_CORE_ASSERT(ensureSkinningCapacity(0), "DeferredFrameResourceSet failed to create initial skinning resources");
}

void DeferredFrameResourceSet::destroy()
{
    _bindings = {};
    _uploadArena.reset();
    _skinningDSP.reset();
    _skinningDSL.reset();
    _ssaoFrameDSP.reset();
    _ssaoFrameDSL.reset();
    _skyboxFrameDSP.reset();
    _skyboxFrameDSL.reset();
    _frameAndLightDSP.reset();
    _frameAndLightDSL.reset();
    _shadowState = {};
    _skinningCapacity = 0;
    _lastShadowedPointLights = 0;
    _render = nullptr;
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

    if (frameData.bHasDirectionalLight) {
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
    constexpr uint32_t maxPaletteCount = std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        return std::nullopt;
    }

    uint32_t nextCapacity = currentCapacity == 0 ? 16u : currentCapacity;
    if (nextCapacity > maxPaletteCount) {
        return std::nullopt;
    }
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }
    return nextCapacity;
}

bool DeferredFrameResourceSet::ensureSkinningCapacity(uint32_t paletteCount)
{
    if (_skinningDSP && std::max(1u, paletteCount) <= _skinningCapacity) {
        return true;
    }

    const auto nextCapacity = calculateSkinningCapacity(_skinningCapacity, paletteCount);
    if (!nextCapacity.has_value()) {
        YA_CORE_ERROR("Deferred skinning palette count {} exceeds buffer size limit", paletteCount);
        return false;
    }

    auto nextDSP = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = "Deferred_Skinning_DSP",
            .maxSets   = MAX_FLIGHTS_IN_FLIGHT,
            .poolSizes = {{.type = EPipelineDescriptorType::StorageBuffer, .descriptorCount = MAX_FLIGHTS_IN_FLIGHT}},
        });
    if (!nextDSP) {
        YA_CORE_ERROR("DeferredFrameResourceSet failed to create skinning descriptor pool");
        return false;
    }

    const uint32_t bufferSize = *nextCapacity * sizeof(RenderSkinningPalette);
    std::array<stdptr<IBuffer>, MAX_FLIGHTS_IN_FLIGHT> nextBuffers{};
    std::array<DescriptorSetHandle, MAX_FLIGHTS_IN_FLIGHT> nextDescriptorSets{};
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        nextBuffers[flightIndex] = _render->getResourceFactory()->createBuffer(
            BufferCreateInfo{
                .label       = std::format("Deferred_Skinning_SSBO_{}", flightIndex),
                .usage       = EBufferUsage::StorageBuffer,
                .size        = bufferSize,
                .memoryUsage = EMemoryUsage::CpuToGpu,
            });
        if (!nextBuffers[flightIndex]) {
            YA_CORE_ERROR("DeferredFrameResourceSet failed to create skinning buffer for flight {}", flightIndex);
            return false;
        }

        nextDescriptorSets[flightIndex] = nextDSP->allocateDescriptorSets(_skinningDSL);
        if (!nextDescriptorSets[flightIndex]) {
            YA_CORE_ERROR("DeferredFrameResourceSet failed to allocate skinning descriptor set for flight {}", flightIndex);
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
    _skinningCapacity = *nextCapacity;
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

bool DeferredFrameResourceSet::prepareSkinning(const RenderStageContext& ctx)
{
    YA_CORE_ASSERT(ctx.frameData != nullptr, "Deferred skinning prepare requires frame data");
    const auto& palettes = ctx.frameData->skinningPalettes;
    if (palettes.size() > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Deferred skinning palette count exceeds uint32 range");
        return false;
    }
    if (!ensureSkinningCapacity(static_cast<uint32_t>(palettes.size()))) {
        return false;
    }
    if (palettes.empty()) {
        return true;
    }

    auto& buffer = _bindings[ctx.flightIndex].skinningBuffer;
    YA_CORE_ASSERT(buffer != nullptr, "Deferred skinning buffer is missing for flight {}", ctx.flightIndex);
    const uint32_t byteCount = static_cast<uint32_t>(palettes.size() * sizeof(RenderSkinningPalette));
    return buffer->writeData(palettes.data(), byteCount, 0) && buffer->flush(byteCount, 0);
}

void DeferredFrameResourceSet::updateDescriptorSet(uint32_t flightIndex, const Binding& binding)
{
    auto& previous = _bindings[flightIndex];
    const bool bChanged = previous.frame.buffer.get() != binding.frame.buffer.get() ||
                          previous.frame.offset != binding.frame.offset ||
                          previous.frame.size != binding.frame.size ||
                          previous.light.buffer.get() != binding.light.buffer.get() ||
                          previous.light.offset != binding.light.offset ||
                          previous.light.size != binding.light.size;
    if (!bChanged) {
        return;
    }

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

void DeferredFrameResourceSet::updateSSAODescriptorSet(uint32_t flightIndex, const Binding& binding)
{
    const auto& previous = _bindings[flightIndex];
    const bool bChanged = previous.ssaoFrame.buffer.get() != binding.ssaoFrame.buffer.get() ||
                          previous.ssaoFrame.offset != binding.ssaoFrame.offset ||
                          previous.ssaoFrame.size != binding.ssaoFrame.size;
    if (!bChanged) {
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.ssaoFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.ssaoFrame.descriptor()}),
    });
}

void DeferredFrameResourceSet::updateSkyboxDescriptorSet(uint32_t flightIndex, const Binding& binding)
{
    const auto& previous = _bindings[flightIndex];
    const bool bChanged = previous.skyboxFrame.buffer.get() != binding.skyboxFrame.buffer.get() ||
                          previous.skyboxFrame.offset != binding.skyboxFrame.offset ||
                          previous.skyboxFrame.size != binding.skyboxFrame.size;
    if (!bChanged) {
        return;
    }

    _render->getDescriptorHelper()->updateDescriptorSets({
        IDescriptorSetHelper::genBufferWrite(
            binding.skyboxFrameDescriptorSet,
            0,
            0,
            EPipelineDescriptorType::UniformBuffer,
            {binding.skyboxFrame.descriptor()}),
    });
}

bool DeferredFrameResourceSet::prepare(const RenderStageContext& ctx)
{
    if (!_render || !_uploadArena || !ctx.frameData || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }

    if (!_uploadArena->beginFlight(ctx.flightIndex)) {
        return false;
    }
    if (!prepareSkinning(ctx)) {
        return false;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    const uint64_t reserveSize = static_cast<uint64_t>(sizeof(FrameData)) +
                                 static_cast<uint64_t>(alignment - 1u) +
                                 static_cast<uint64_t>(sizeof(LightData));
    if (reserveSize > std::numeric_limits<uint32_t>::max()) {
        YA_CORE_ERROR("Deferred frame/light upload reservation exceeds 32-bit buffer size");
        return false;
    }

    const auto reservation = _uploadArena->allocate(
        ctx.flightIndex,
        static_cast<uint32_t>(reserveSize),
        alignment);
    if (!reservation.has_value()) {
        // Keep the last complete binding usable when a transient allocation
        // fails. The current flight fence has already completed, so the old
        // slice remains a valid fallback until the next successful prepare.
        return false;
    }

    const uint64_t lightOffset =
        ((reservation->offset + sizeof(FrameData) + alignment - 1u) / alignment) * alignment;
    const uint64_t reservationEnd = lightOffset + sizeof(LightData);
    YA_CORE_ASSERT(
        reservationEnd <= reservation->offset + reservation->size,
        "Deferred frame/light upload reservation does not cover both aligned slices");

    const FrameUploadArena::Allocation frame{
        .buffer = reservation->buffer,
        .offset = reservation->offset,
        .size   = sizeof(FrameData),
    };
    const FrameUploadArena::Allocation light{
        .buffer = reservation->buffer,
        .offset = lightOffset,
        .size   = sizeof(LightData),
    };

    FrameData frameData{
        .viewPos    = ctx.frameData->cameraPos,
        .viewMatrix = ctx.frameData->view,
        .projMatrix = ctx.frameData->projection,
    };
    const auto lightData = buildLightData(*ctx.frameData);
    _lastShadowedPointLights = std::min({
        _shadowState.maxShadowedPointLights,
        ctx.frameData->numPointLights,
        static_cast<uint32_t>(MAX_POINT_LIGHTS),
    });
    if (!frame.write(&frameData, sizeof(frameData)) || !light.write(&lightData, sizeof(lightData))) {
        return false;
    }

    Binding next = _bindings[ctx.flightIndex];
    next.frame   = frame;
    next.light   = light;
    updateDescriptorSet(ctx.flightIndex, next);
    _bindings[ctx.flightIndex] = std::move(next);
    return true;
}

bool DeferredFrameResourceSet::prepareSSAO(
    const RenderStageContext& ctx,
    const SSAOFrameData& frameData)
{
    if (!_render || !_uploadArena || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    const auto ssaoFrame = _uploadArena->allocate(
        ctx.flightIndex,
        sizeof(SSAOFrameData),
        alignment);
    if (!ssaoFrame.has_value() || !ssaoFrame->write(&frameData, sizeof(frameData))) {
        return false;
    }

    Binding next   = _bindings[ctx.flightIndex];
    next.ssaoFrame = *ssaoFrame;
    updateSSAODescriptorSet(ctx.flightIndex, next);
    _bindings[ctx.flightIndex] = std::move(next);
    return true;
}

bool DeferredFrameResourceSet::prepareSkybox(
    const RenderStageContext& ctx,
    const SkyboxFrameData& frameData)
{
    if (!_render || !_uploadArena || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }

    const uint32_t alignment = std::max(_render->getUniformBufferOffsetAlignment(), 1u);
    const auto skyboxFrame = _uploadArena->allocate(
        ctx.flightIndex,
        sizeof(SkyboxFrameData),
        alignment);
    if (!skyboxFrame.has_value() || !skyboxFrame->write(&frameData, sizeof(frameData))) {
        return false;
    }

    Binding next     = _bindings[ctx.flightIndex];
    next.skyboxFrame = *skyboxFrame;
    updateSkyboxDescriptorSet(ctx.flightIndex, next);
    _bindings[ctx.flightIndex] = std::move(next);
    return true;
}

const DeferredFrameResourceSet::Binding& DeferredFrameResourceSet::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "DeferredFrameResourceSet invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
