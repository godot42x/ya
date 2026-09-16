#pragma once

#include "BasicShadowMap/BasicShadowPayload.h"

#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "Render3D/Common/PerFlightFrameResourceSetBase.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/Common/RenderViewBindingTable.h"
#include "Render3D/Common/Shadow/ShadowTypes.h"

#include "CombineShadowMappingGenerate.slang.h"
#include "Shadow.PointShadowIndirect.slang.h"

#include <array>
#include <cstdint>

namespace ya
{

struct IRender;
class RenderSubmission;

/**
 * Owns Shadow's persistent layouts and skinning storage.
 *
 * Layouts are device-lifetime. Cascade/face descriptor sets and upload slices
 * are allocated from `RenderSubmission` so a second View cannot overwrite the
 * first. Skinning palettes stay submission-scoped.
 */
class ShadowFrameResources : public PerFlightFrameResourceSetBase<ShadowFrameResources>
{
    friend class PerFlightFrameResourceSetBase<ShadowFrameResources>;

  public:
    using DirectionalFrameData = slang_types::CombineShadowMappingGenerate::FrameData;
    using PointFaceData        = slang_types::Shadow::PointShadowIndirect::PointShadowFaceData;

    struct ViewPayloads
    {
        uint32_t directionalCount = 0;
        std::array<DirectionalFrameData, MAX_DIRECTIONAL_CASCADES> directional{};
        uint32_t pointFaceCount = 0;
        std::array<PointFaceData, ShadowConstants::POINT_SHADOW_FACE_COUNT> pointFaces{};
    };

    struct Binding
    {
        std::array<FrameUploadArena::Allocation, MAX_DIRECTIONAL_CASCADES> directionalFrames{};
        std::array<DescriptorSetHandle, MAX_DIRECTIONAL_CASCADES>           directionalFrameDS{};
        std::array<FrameUploadArena::Allocation, ShadowConstants::POINT_SHADOW_FACE_COUNT> pointFaces{};
        std::array<DescriptorSetHandle, ShadowConstants::POINT_SHADOW_FACE_COUNT>           pointFaceDS{};
        stdptr<IBuffer>        skinningBuffer;
        DescriptorSetHandle    skinningDS{};

        [[nodiscard]] bool isValid() const
        {
            return skinningDS && skinningBuffer;
        }
    };

    struct SkinningBinding
    {
        DescriptorSetHandle skinningDescriptorSet{};
        stdptr<IBuffer>     skinningBuffer;
    };

    void init(IRender* render);
    void destroy();

    const Binding* beginView(RenderSubmission&              submission,
                             RenderViewRecordingContext&    view,
                             const BasicShadowFramePayload& payload);

    bool prepareSkinning(const RenderStageContext& ctx)
    {
        return PerFlightFrameResourceSetBase<ShadowFrameResources>::prepareSkinning(ctx);
    }

    static bool writeViewPayloads(FrameUploadArena&   arena,
                                  uint32_t            flightIndex,
                                  uint32_t            alignment,
                                  const ViewPayloads& payloads,
                                  Binding&            binding);
    static bool writeViewPayloads(RenderSubmission&   submission,
                                  uint32_t            alignment,
                                  const ViewPayloads& payloads,
                                  Binding&            binding);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameDSL() const { return _frameDSL; }
    [[nodiscard]] const Binding*               getViewBinding(uint32_t flightIndex, uint32_t viewSlot) const;
    [[nodiscard]] uint32_t                     liveViewCount(uint32_t flightIndex) const;

  private:
    stdptr<IDescriptorSetLayout> _frameDSL;
    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT> _skinningBindings{};
    RenderViewBindingTable<Binding> _viewBindings;

    std::array<SkinningBinding, MAX_FLIGHTS_IN_FLIGHT>& bindings() { return _skinningBindings; }

    bool ensureViewDescriptors(RenderSubmission& submission, Binding& binding, uint32_t directionalCount, uint32_t pointFaceCount);
    void updateViewDescriptors(const Binding& binding, uint32_t directionalCount, uint32_t pointFaceCount);
    static ViewPayloads buildViewPayloads(const BasicShadowFramePayload& payload);
};

} // namespace ya
