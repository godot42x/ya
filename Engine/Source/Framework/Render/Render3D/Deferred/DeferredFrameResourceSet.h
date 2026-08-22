#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"

#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.LightPass.slang.h"
#include "DeferredRender.SSAO.slang.h"
#include "GLSL.Skybox.glsl.h"

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
class YA_RENDER_3D_API DeferredFrameResourceSet : public PerFlightFrameResourceSetBase<DeferredFrameResourceSet>
{
    friend class PerFlightFrameResourceSetBase<DeferredFrameResourceSet>;
    friend class DeferredFrameResourceSetTestAccess;

  public:
    using FrameData = slang_types::DeferredRender::GBufferPass_PBR::FrameData;
    using LightData = slang_types::DeferredRender::LightPass::LightData;
    using SSAOFrameData = slang_types::DeferredRender::SSAO::FrameData;
    using SkyboxFrameData = glsl_types::GLSL::Skybox::FrameUBO;

    struct Binding
    {
        DescriptorSetHandle             frameAndLightDescriptorSet{};
        DescriptorSetHandle             skinningDescriptorSet{};
        DescriptorSetHandle             ssaoFrameDescriptorSet{};
        DescriptorSetHandle             skyboxFrameDescriptorSet{};
        FrameUploadArena::Allocation    frame;
        FrameUploadArena::Allocation    light;
        FrameUploadArena::Allocation    ssaoFrame;
        FrameUploadArena::Allocation    skyboxFrame;
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
    /** Upload skybox camera parameters into the current flight's shared frame arena. */
    bool prepareSkybox(const RenderStageContext& ctx, const SkyboxFrameData& frameData);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameAndLightDSL() const { return _frameAndLightDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSSAOFrameDSL() const { return _ssaoFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxFrameDSL() const { return _skyboxFrameDSL; }
    [[nodiscard]] const Binding&               getBinding(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getMaxShadowedPointLights() const { return _shadowState.maxShadowedPointLights; }
    [[nodiscard]] uint32_t getLastShadowedPointLights() const { return _lastShadowedPointLights; }

  private:
    stdptr<IDescriptorSetLayout>      _frameAndLightDSL;
    stdptr<IDescriptorPool>           _frameAndLightDSP;
    stdptr<IDescriptorSetLayout>      _ssaoFrameDSL;
    stdptr<IDescriptorPool>           _ssaoFrameDSP;
    stdptr<IDescriptorSetLayout>      _skyboxFrameDSL;
    stdptr<IDescriptorPool>           _skyboxFrameDSP;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    ShadowRuntimeState _shadowState{};
    uint32_t _lastShadowedPointLights = 0;

    /// CRTP contract: per-flight skinning slots consumed by the shared base.
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT>& bindings() { return _bindings; }

    [[nodiscard]] LightData buildLightData(const RenderFrameData& frameData) const;
    [[nodiscard]] static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount);
    void updateDescriptorSet(uint32_t flightIndex, const Binding& binding);
    void updateSSAODescriptorSet(uint32_t flightIndex, const Binding& binding);
    void updateSkyboxDescriptorSet(uint32_t flightIndex, const Binding& binding);
};

} // namespace ya
