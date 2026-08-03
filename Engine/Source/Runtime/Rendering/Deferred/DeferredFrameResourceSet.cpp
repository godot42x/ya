#include "DeferredFrameResourceSet.h"

#include "Core/Log.h"
#include "Render/Render.h"

#include <algorithm>
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

    _uploadArena = std::make_unique<FrameUploadArena>(
        *render->getResourceFactory(),
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer,
        "Deferred.FrameUpload");

    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        _bindings[flightIndex] = Binding{
            .frameAndLightDescriptorSet = _frameAndLightDSP->allocateDescriptorSets(_frameAndLightDSL),
        };
    }
}

void DeferredFrameResourceSet::destroy()
{
    _bindings = {};
    _uploadArena.reset();
    _frameAndLightDSP.reset();
    _frameAndLightDSL.reset();
    _shadowState = {};
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

bool DeferredFrameResourceSet::prepare(const RenderStageContext& ctx)
{
    if (!_render || !_uploadArena || !ctx.frameData || ctx.flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return false;
    }

    if (!_uploadArena->beginFlight(ctx.flightIndex)) {
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

const DeferredFrameResourceSet::Binding& DeferredFrameResourceSet::getBinding(uint32_t flightIndex) const
{
    YA_CORE_ASSERT(flightIndex < _bindings.size(), "DeferredFrameResourceSet invalid flight index {}", flightIndex);
    return _bindings[flightIndex];
}

} // namespace ya
