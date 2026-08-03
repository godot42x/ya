#pragma once

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameUploadArena.h"
#include "Render/Stage/IRenderStage.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowRuntimeState.h"

#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.LightPass.slang.h"
#include "DeferredRender.SSAO.slang.h"

#include <array>
#include <optional>

namespace ya
{

/**
 * Owns Deferred's shared frame/light descriptor set and its per-flight data.
 *
 * The descriptor set layout and descriptor sets are pipeline resources. The
 * frame and light payloads are frame-local slices in a per-flight upload arena;
 * skinning uses capacity-managed per-flight storage buffers. The graph imports
 * those owner-backed resources after this object has prepared the current
 * flight.
 */
class DeferredFrameResourceSet
{
    friend class DeferredFrameResourceSetTestAccess;

  public:
    using FrameData = slang_types::DeferredRender::GBufferPass_PBR::FrameData;
    using LightData = slang_types::DeferredRender::LightPass::LightData;
    using SSAOFrameData = slang_types::DeferredRender::SSAO::FrameData;

    struct Binding
    {
        DescriptorSetHandle             frameAndLightDescriptorSet{};
        DescriptorSetHandle             skinningDescriptorSet{};
        DescriptorSetHandle             ssaoFrameDescriptorSet{};
        FrameUploadArena::Allocation    frame;
        FrameUploadArena::Allocation    light;
        FrameUploadArena::Allocation    ssaoFrame;
        stdptr<IBuffer>                  skinningBuffer;

        [[nodiscard]] bool isValid() const
        {
            return frameAndLightDescriptorSet && skinningDescriptorSet &&
                   frame.valid() && light.valid() && skinningBuffer;
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
    /** Upload SSAO parameters into the current flight's shared frame arena. */
    bool prepareSSAO(const RenderStageContext& ctx, const SSAOFrameData& frameData);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameAndLightDSL() const { return _frameAndLightDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSSAOFrameDSL() const { return _ssaoFrameDSL; }
    [[nodiscard]] const Binding&               getBinding(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getMaxShadowedPointLights() const { return _shadowState.maxShadowedPointLights; }
    [[nodiscard]] uint32_t getLastShadowedPointLights() const { return _lastShadowedPointLights; }

  private:
    IRender* _render = nullptr;
    std::unique_ptr<FrameUploadArena> _uploadArena;
    stdptr<IDescriptorSetLayout>      _frameAndLightDSL;
    stdptr<IDescriptorPool>           _frameAndLightDSP;
    stdptr<IDescriptorSetLayout>      _skinningDSL;
    stdptr<IDescriptorPool>           _skinningDSP;
    stdptr<IDescriptorSetLayout>      _ssaoFrameDSL;
    stdptr<IDescriptorPool>           _ssaoFrameDSP;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    ShadowRuntimeState _shadowState{};
    uint32_t _skinningCapacity = 0;
    uint32_t _lastShadowedPointLights = 0;

    [[nodiscard]] LightData buildLightData(const RenderFrameData& frameData) const;
    [[nodiscard]] static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount);
    bool ensureSkinningCapacity(uint32_t paletteCount);
    bool prepareSkinning(const RenderStageContext& ctx);
    void updateDescriptorSet(uint32_t flightIndex, const Binding& binding);
    void updateSSAODescriptorSet(uint32_t flightIndex, const Binding& binding);
};

} // namespace ya
