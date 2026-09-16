#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"

#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.LightPass.slang.h"
#include "DeferredRender.SSAO.slang.h"
#include "Skybox.slang.h"

#include <array>
#include <optional>

namespace ya
{

struct IRender;
class RenderSubmission;

/**
 * Owns Deferred's persistent layouts and skinning storage.
 *
 * Layouts are device-lifetime. Frame/light/SSAO/skybox descriptor sets and
 * upload slices are allocated from `RenderSubmission` so a second View cannot
 * overwrite the first. Skinning palettes stay submission-scoped (shared by
 * Views of the same Scene).
 */
class YA_RENDER_3D_API DeferredFrameResourceSet : public PerFlightFrameResourceSetBase<DeferredFrameResourceSet>
{
    friend class PerFlightFrameResourceSetBase<DeferredFrameResourceSet>;
    friend class DeferredFrameResourceSetTestAccess;

  public:
    using FrameData = slang_types::DeferredRender::GBufferPass_PBR::FrameData;
    using LightData = slang_types::DeferredRender::LightPass::LightData;
    using SSAOFrameData = slang_types::DeferredRender::SSAO::FrameData;
    using SkyboxFrameData = slang_types::Skybox::FrameUBO;

    struct ViewPayloads
    {
        FrameData            frame{};
        LightData            light{};
        const SSAOFrameData* ssao   = nullptr;
        const SkyboxFrameData* skybox = nullptr;
    };

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

    /// Flight-local skinning storage consumed by PerFlightFrameResourceSetBase.
    struct SkinningBinding
    {
        DescriptorSetHandle skinningDescriptorSet{};
        stdptr<IBuffer>     skinningBuffer;
    };

    void init(IRender* render);
    void destroy();

    void applyShadowState(const ShadowRuntimeState& shadowState)
    {
        _shadowState = shadowState;
    }

    /// Upload this View's frame/light (and optional SSAO/skybox) slices into a
    /// new Binding slot. On success, `view.viewSlot` is the live index and the
    /// returned Binding stays stable for the rest of the submission.
    const Binding* beginView(RenderSubmission&           submission,
                             RenderViewRecordingContext& view,
                             const SSAOFrameData*        ssao   = nullptr,
                             const SkyboxFrameData*      skybox = nullptr);

    bool prepareSkinning(const RenderStageContext& ctx)
    {
        return PerFlightFrameResourceSetBase<DeferredFrameResourceSet>::prepareSkinning(ctx);
    }

    /// Write View UBO slices into `binding` without touching descriptor sets.
    static bool writeViewPayloads(FrameUploadArena&   arena,
                                  uint32_t            flightIndex,
                                  uint32_t            alignment,
                                  const ViewPayloads& payloads,
                                  Binding&            binding);
    static bool writeViewPayloads(RenderSubmission&   submission,
                                  uint32_t            alignment,
                                  const ViewPayloads& payloads,
                                  Binding&            binding);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameAndLightDSL() const { return _frameAndLightDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSSAOFrameDSL() const { return _ssaoFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxFrameDSL() const { return _skyboxFrameDSL; }
    [[nodiscard]] const Binding*               getViewBinding(uint32_t flightIndex, uint32_t viewSlot) const;
    [[nodiscard]] uint32_t                     liveViewCount(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getMaxShadowedPointLights() const { return _shadowState.maxShadowedPointLights; }
    [[nodiscard]] uint32_t getLastShadowedPointLights() const { return _lastShadowedPointLights; }

  private:
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    stdptr<IDescriptorSetLayout> _ssaoFrameDSL;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT> _skinningBindings{};
    RenderViewBindingTable<Binding> _viewBindings;
    ShadowRuntimeState _shadowState{};
    uint32_t _lastShadowedPointLights = 0;

    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT>& bindings() { return _skinningBindings; }

    [[nodiscard]] LightData buildLightData(const RenderFrameData& frameData) const;
    [[nodiscard]] static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount);
    bool ensureViewDescriptors(RenderSubmission& submission, Binding& binding);
    void updateFrameAndLightDescriptorSet(const Binding& binding);
    void updateSSAODescriptorSet(const Binding& binding);
    void updateSkyboxDescriptorSet(const Binding& binding);
};

} // namespace ya
