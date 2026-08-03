#pragma once

#include "BasicShadowMap/BasicShadowPayload.h"

#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameUploadArena.h"
#include "Render/Stage/IRenderStage.h"

#include "Runtime/Rendering/Common/Shadow/ShadowTypes.h"

#include "CombineShadowMappingGenerate.slang.h"
#include "Shadow.PointShadowIndirect.slang.h"

#include <array>
#include <memory>

namespace ya
{

struct IRender;

/**
 * Owns the host-written frame inputs shared by the directional and point
 * shadow raster passes. The pass modules keep pipeline state only; this
 * object owns descriptor layouts, per-flight descriptors, upload slices and
 * the capacity-managed skinning buffer.
 */
class ShadowFrameResources
{
  public:
    using DirectionalFrameData = slang_types::CombineShadowMappingGenerate::FrameData;
    using PointFaceData        = slang_types::Shadow::PointShadowIndirect::PointShadowFaceData;

    struct Binding
    {
        std::array<FrameUploadArena::Allocation, MAX_DIRECTIONAL_CASCADES> directionalFrames{};
        std::array<DescriptorSetHandle, MAX_DIRECTIONAL_CASCADES>           directionalFrameDS{};
        std::array<FrameUploadArena::Allocation, ShadowConstants::POINT_SHADOW_FACE_COUNT> pointFaces{};
        std::array<DescriptorSetHandle, ShadowConstants::POINT_SHADOW_FACE_COUNT>           pointFaceDS{};
        stdptr<IBuffer>        skinningBuffer;
        DescriptorSetHandle    skinningDS{};
    };

    void init(IRender* render);
    void destroy();

    /** Begin the flight and upload all host-written shadow inputs. */
    bool prepare(const BasicShadowFramePayload& payload);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getFrameDSL() const { return _frameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }
    [[nodiscard]] const Binding& getBinding(uint32_t flightIndex) const;

  private:
    static std::optional<uint32_t> calculateSkinningCapacity(
        uint32_t currentCapacity,
        uint32_t paletteCount);
    bool ensureSkinningCapacity(uint32_t paletteCount);

    IRender* _render = nullptr;
    std::unique_ptr<FrameUploadArena> _uploadArena;
    stdptr<IDescriptorSetLayout>      _frameDSL;
    stdptr<IDescriptorSetLayout>      _skinningDSL;
    stdptr<IDescriptorPool>           _descriptorPool;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    uint32_t _skinningCapacity = 0;
};

} // namespace ya
