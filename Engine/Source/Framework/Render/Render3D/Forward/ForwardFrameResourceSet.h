#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Common/ViewDescriptorSetAllocator.h"
#include "Render3D/Stage/IRenderStage.h"

#include "PBRForward.slang.h"
#include "PhongLit.slang.h"
#include "Skybox.slang.h"
#include "Unlit.slang.h"

#include <array>

namespace ya
{

struct IBuffer;
struct IRender;

/**
 * Owns Forward's persistent layouts/pools and the per-flight upload arena.
 *
 * Layouts and descriptor pools are device-lifetime. Upload slices and
 * frame/light/skybox descriptor sets are View-owned: beginSubmission() opens
 * a flight token, beginView() acquires an independent Binding so a second
 * View cannot overwrite the first. Skinning palettes stay submission-scoped
 * (shared by Views of the same Scene).
 */
class ForwardFrameResourceSet : public PerFlightFrameResourceSetBase<ForwardFrameResourceSet>
{
    friend class PerFlightFrameResourceSetBase<ForwardFrameResourceSet>;

  public:
    using PBRFrameUBO    = slang_types::PBRForward::FrameData;
    using PBRLightUBO    = slang_types::PBRForward::LightData;
    using PhongFrameUBO  = slang_types::PhongLit::FrameData;
    using PhongLightUBO  = slang_types::PhongLit::LightData;
    using PhongDebugUBO  = slang_types::PhongLit::DebugData;
    using UnlitFrameUBO  = slang_types::Unlit::FrameUBO;
    using SkyboxFrameUBO = slang_types::Skybox::FrameUBO;

    /// CPU payloads built by the viewport stage for the current View.
    struct FramePayloads
    {
        PBRFrameUBO    pbrFrame{};
        PBRLightUBO    pbrLight{};
        PhongFrameUBO  phongFrame{};
        PhongLightUBO  phongLight{};
        PhongDebugUBO  phongDebug{};
        UnlitFrameUBO  unlitFrame{};
        SkyboxFrameUBO skyboxFrame{};
    };

    struct Binding
    {
        DescriptorSetHandle skinningDescriptorSet{};
        DescriptorSetHandle pbrFrameDescriptorSet{};
        DescriptorSetHandle phongFrameDescriptorSet{};
        DescriptorSetHandle unlitFrameDescriptorSet{};
        DescriptorSetHandle skyboxFrameDescriptorSet{};
        stdptr<IBuffer>     skinningBuffer;

        FrameUploadArena::Allocation pbrFrame{};
        FrameUploadArena::Allocation pbrLight{};
        FrameUploadArena::Allocation phongFrame{};
        FrameUploadArena::Allocation phongLight{};
        FrameUploadArena::Allocation phongDebug{};
        FrameUploadArena::Allocation unlitFrame{};
        FrameUploadArena::Allocation skyboxFrame{};

        [[nodiscard]] bool isValid() const
        {
            return skinningDescriptorSet && skinningBuffer &&
                   pbrFrameDescriptorSet && phongFrameDescriptorSet &&
                   unlitFrameDescriptorSet && skyboxFrameDescriptorSet &&
                   pbrFrame.valid() && pbrLight.valid();
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

    bool beginSubmission(const RenderSubmissionContext& submission);
    /// Upload this View's frame/light/skybox slices into a new Binding slot.
    /// On success, `view.viewSlot` is the live index and the returned Binding
    /// stays stable for the rest of the submission.
    const Binding* beginView(const RenderSubmissionContext& submission,
                             RenderViewRecordingContext&    view,
                             const FramePayloads&           payloads);

    bool prepareSkinning(const RenderStageContext& ctx)
    {
        return PerFlightFrameResourceSetBase<ForwardFrameResourceSet>::prepareSkinning(ctx);
    }

    /// Write View UBO slices into `binding` without touching descriptor sets.
    /// beginView uses this, then updates that slot's descriptor sets.
    static bool writeViewPayloads(FrameUploadArena& arena,
                                  uint32_t          flightIndex,
                                  uint32_t          alignment,
                                  const FramePayloads& payloads,
                                  Binding&          binding);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getPBRFrameDSL() const { return _pbrFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getPhongFrameDSL() const { return _phongFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getUnlitFrameDSL() const { return _unlitFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxFrameDSL() const { return _skyboxFrameDSL; }
    [[nodiscard]] const Binding*               getViewBinding(uint32_t flightIndex, uint32_t viewSlot) const;
    [[nodiscard]] uint32_t                     liveViewCount(uint32_t flightIndex) const;

  private:
    stdptr<IDescriptorSetLayout> _pbrFrameDSL;
    ViewDescriptorSetAllocator   _pbrFrameSets;
    stdptr<IDescriptorSetLayout> _phongFrameDSL;
    ViewDescriptorSetAllocator   _phongFrameSets;
    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    ViewDescriptorSetAllocator   _unlitFrameSets;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    ViewDescriptorSetAllocator   _skyboxFrameSets;
    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT> _skinningBindings{};
    RenderViewBindingTable<Binding> _viewBindings;

    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT>& bindings() { return _skinningBindings; }

    bool ensureViewDescriptors(Binding& binding);
    void                    updatePBRFrameDescriptorSet(const Binding& binding);
    void                    updatePhongFrameDescriptorSet(const Binding& binding);
    void                    updateUnlitFrameDescriptorSet(const Binding& binding);
    void                    updateSkyboxFrameDescriptorSet(const Binding& binding);
};

} // namespace ya
