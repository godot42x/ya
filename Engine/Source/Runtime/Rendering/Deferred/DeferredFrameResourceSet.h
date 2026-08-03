#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameUploadArena.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowRuntimeState.h"

#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.LightPass.slang.h"

#include <array>

namespace ya
{

/**
 * Owns Deferred's shared frame/light descriptor set and its per-flight data.
 *
 * The descriptor set layout and descriptor sets are pipeline resources. The
 * frame and light payloads are frame-local slices in a per-flight upload arena;
 * the graph imports those slices' backing buffer after this object has written
 * them for the current flight.
 */
class DeferredFrameResourceSet
{
  public:
    using FrameData = slang_types::DeferredRender::GBufferPass_PBR::FrameData;
    using LightData = slang_types::DeferredRender::LightPass::LightData;

    struct Binding
    {
        DescriptorSetHandle             frameAndLightDescriptorSet{};
        FrameUploadArena::Allocation    frame;
        FrameUploadArena::Allocation    light;

        [[nodiscard]] bool isValid() const
        {
            return frameAndLightDescriptorSet && frame.valid() && light.valid();
        }
    };

    void init(IRender* render);
    void destroy();

    void applyShadowState(const ShadowRuntimeState& shadowState)
    {
        _shadowState = shadowState;
    }

    /** Upload the current frame and light payloads for the fence-safe flight. */
    bool prepare(const RenderStageContext& ctx);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameAndLightDSL() const { return _frameAndLightDSL; }
    [[nodiscard]] const Binding&               getBinding(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getMaxShadowedPointLights() const { return _shadowState.maxShadowedPointLights; }
    [[nodiscard]] uint32_t getLastShadowedPointLights() const { return _lastShadowedPointLights; }

  private:
    IRender* _render = nullptr;
    std::unique_ptr<FrameUploadArena> _uploadArena;
    stdptr<IDescriptorSetLayout>      _frameAndLightDSL;
    stdptr<IDescriptorPool>           _frameAndLightDSP;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    ShadowRuntimeState _shadowState{};
    uint32_t _lastShadowedPointLights = 0;

    [[nodiscard]] LightData buildLightData(const RenderFrameData& frameData) const;
    void updateDescriptorSet(uint32_t flightIndex, const Binding& binding);
};

} // namespace ya
