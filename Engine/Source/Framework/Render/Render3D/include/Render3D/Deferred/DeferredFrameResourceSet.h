#pragma once

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Stage/IRenderStage.h"
#include "Render3D/Common/Shadow/Common/ShadowRuntimeState.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Common/ViewPassResources.h"

#include "DeferredRender.GBufferPass_PBR.slang.h"
#include "DeferredRender.LightPass.slang.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace ya
{

struct IRender;
class RenderSubmission;

/**
 * Owns Deferred's shared frame/light layout and scene-family skinning storage.
 *
 * Frame/light descriptor sets and upload slices are allocated from
 * `RenderSubmission` so a second View cannot overwrite the first. Feature-local
 * payloads (SSAO frame UBO, skybox frame UBO, overlay/entity/postprocess sets)
 * live in the features' own prepared bindings. Skinning palettes belong to the
 * Scene family on `RenderSubmission`.
 */
class YA_RENDER_3D_API DeferredFrameResourceSet : public PerFlightFrameResourceSetBase
{
  public:
    using FrameData = slang_types::DeferredRender::GBufferPass_PBR::FrameData;
    using LightData = slang_types::DeferredRender::LightPass::LightData;

    /// The deferred shader's light-instance storage buffer element (see
    /// LightPass.slang's uLightInstances): layout owned by the generated
    /// header.
    using LightInstanceGpu = slang_types::DeferredRender::LightPass::LightInstance;

    struct ViewPayloads
    {
        FrameData            frame{};
        LightData            light{};
        /// The frame's point lights as the shader sees them, uploaded as one
        /// count-sized storage buffer slice. Built by
        /// `buildLightInstanceBuffer`, owned here through the upload.
        std::vector<LightInstanceGpu> lightInstances;
    };

    struct Binding
    {
        DescriptorSetHandle             frameAndLightDescriptorSet{};
        DescriptorSetHandle             skinningDescriptorSet{};
        FrameUploadArena::Allocation    frame;
        FrameUploadArena::Allocation    light;
        FrameUploadArena::Allocation    lightInstances;
        stdptr<IBuffer>                  skinningBuffer;

        [[nodiscard]] bool isValid() const
        {
            return frameAndLightDescriptorSet && skinningDescriptorSet &&
                   frame.valid() && light.valid() && skinningBuffer;
        }
    };

    /// Typed View owner: the shared frame/light UBOs and skinning buffer the
    /// View's passes read from. Feature-owned sets (SSAO, skybox, lighting
    /// GBuffer/shadow, entity id, overlay, sprite, postprocess) live in the
    /// features' own Prepared values; the bag is deliberately gone.
    /// (No ViewResources struct: the binding IS the View slot.)

    void init(IRender* render);
    void destroy();

    void applyShadowState(const ShadowRuntimeState& shadowState)
    {
        _shadowState = shadowState;
    }

    /// Upload this View's shared frame/light/skinning slices into a new Binding
    /// slot. On success, `view.viewSlot` is the live index and the returned
    /// Binding stays stable for the rest of the submission.
    const Binding* beginView(RenderSubmission&           submission,
                             RenderViewRecordingContext& view);

    bool prepareSkinning(RenderSubmission& submission, const RenderViewRecordingContext& view);

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
    [[nodiscard]] const Binding*               getViewBinding(uint32_t flightIndex, uint32_t viewSlot) const;
    [[nodiscard]] uint32_t                     liveViewCount(uint32_t flightIndex) const;
    [[nodiscard]] uint32_t getMaxShadowedPointLights() const { return _shadowState.maxShadowedPointLights; }
    [[nodiscard]] uint32_t getLastShadowedPointLights() const { return _lastShadowedPointLights; }

    [[nodiscard]] LightData buildLightData(const RenderFrameData& frameData) const;
    /// The storage-buffer image of the frame's local lights: field-mapped
    /// (never reinterpreted -- the staging struct carries shadow matrices the
    /// GPU element does not), omni-only shadow budget. `shadowSlotsUsed`
    /// reports how many budget slots the list consumed (spots skip).
    [[nodiscard]] static std::vector<LightInstanceGpu>
    buildLightInstanceBuffer(const RenderFrameData& frameData, uint32_t shadowedPointLightBudget, uint32_t* shadowSlotsUsed = nullptr);

  private:
    stdptr<IDescriptorSetLayout> _frameAndLightDSL;
    RenderViewBindingTable<Binding> _viewBindings;
    ShadowRuntimeState _shadowState{};
    uint32_t _lastShadowedPointLights = 0;


    /// The storage-buffer image of the frame's point lights: field-mapped
    /// (never reinterpreted -- the staging struct carries shadow matrices the
    /// GPU element does not), omni-only shadow budget. `shadowSlotsUsed`
    /// reports how many budget slots the list consumed (spots skip).

    bool ensureViewDescriptors(RenderSubmission& submission, Binding& binding);
    void updateFrameAndLightDescriptorSet(const Binding& binding);
};

} // namespace ya
