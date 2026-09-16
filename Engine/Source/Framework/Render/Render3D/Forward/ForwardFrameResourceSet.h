#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Stage/IRenderStage.h"

#include "GLSL.Skybox.glsl.h"
#include "PBRForward.slang.h"
#include "PhongLit.slang.h"
#include "Test.Unlit.glsl.h"

#include <array>
#include <memory>
#include <vector>

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
    using UnlitFrameUBO  = glsl_types::Test::Unlit::FrameUBO;
    using SkyboxFrameUBO = glsl_types::GLSL::Skybox::FrameUBO;

    static constexpr uint32_t kViewDescriptorChunk = 8;

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
    std::vector<stdptr<IDescriptorPool>> _pbrFrameDSPs;
    uint32_t _pbrAllocatedSets = 0;
    stdptr<IDescriptorSetLayout> _phongFrameDSL;
    std::vector<stdptr<IDescriptorPool>> _phongFrameDSPs;
    uint32_t _phongAllocatedSets = 0;
    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    std::vector<stdptr<IDescriptorPool>> _unlitFrameDSPs;
    uint32_t _unlitAllocatedSets = 0;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    std::vector<stdptr<IDescriptorPool>> _skyboxFrameDSPs;
    uint32_t _skyboxAllocatedSets = 0;
    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT> _skinningBindings{};
    RenderViewBindingTable<Binding> _viewBindings;

    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT>& bindings() { return _skinningBindings; }

    stdptr<IDescriptorPool> createViewDescriptorPool(const char* label, uint32_t descriptorCount);
    DescriptorSetHandle     allocateViewSet(std::vector<stdptr<IDescriptorPool>>& pools,
                                            uint32_t&                             allocatedSets,
                                            const stdptr<IDescriptorSetLayout>&   layout,
                                            const char*                           poolLabel,
                                            uint32_t                              descriptorsPerSet);
    bool                    ensureViewDescriptors(Binding& binding);
    void                    updatePBRFrameDescriptorSet(const Binding& binding);
    void                    updatePhongFrameDescriptorSet(const Binding& binding);
    void                    updateUnlitFrameDescriptorSet(const Binding& binding);
    void                    updateSkyboxFrameDescriptorSet(const Binding& binding);
};

} // namespace ya
