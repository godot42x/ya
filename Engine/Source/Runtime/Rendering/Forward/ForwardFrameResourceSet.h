#pragma once

#include "Core/Common/Types.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/FrameUploadArena.h"
#include "Render/Stage/IRenderStage.h"

#include "GLSL.Skybox.glsl.h"
#include "PBRForward.slang.h"
#include "PhongLit.slang.h"
#include "Test.Unlit.glsl.h"

#include <array>
#include <memory>

namespace ya
{

struct IBuffer;
struct IRender;

/**
 * Owns Forward's shared per-flight frame/light/skybox/skinning resources.
 *
 * Frame/light/skybox payloads are frame-local slices in a per-flight upload
 * arena; skinning uses capacity-managed per-flight storage buffers. Stages only
 * borrow the current flight's descriptor sets and build typed CPU payloads;
 * they never own the per-flight GPU buffers themselves (FG-701).
 */
class ForwardFrameResourceSet
{
  public:
    using PBRFrameUBO    = slang_types::PBRForward::FrameData;
    using PBRLightUBO    = slang_types::PBRForward::LightData;
    using PhongFrameUBO  = slang_types::PhongLit::FrameData;
    using PhongLightUBO  = slang_types::PhongLit::LightData;
    using PhongDebugUBO  = slang_types::PhongLit::DebugData;
    using UnlitFrameUBO  = glsl_types::Test::Unlit::FrameUBO;
    using SkyboxFrameUBO = glsl_types::GLSL::Skybox::FrameUBO;

    /// CPU payloads built by the viewport stage for the current frame.
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

        [[nodiscard]] bool isValid() const
        {
            return skinningDescriptorSet && skinningBuffer &&
                   pbrFrameDescriptorSet && phongFrameDescriptorSet &&
                   unlitFrameDescriptorSet && skyboxFrameDescriptorSet;
        }
    };

    void init(IRender* render);
    void destroy();

    /** Upload the current frame's skinning palettes for the fence-safe flight. */
    bool prepareSkinning(const RenderStageContext& ctx);
    /** Upload all frame/light/skybox payloads into the current flight's arena. */
    bool prepareFramePayloads(const RenderStageContext& ctx, const FramePayloads& payloads);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getPBRFrameDSL() const { return _pbrFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getPhongFrameDSL() const { return _phongFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getUnlitFrameDSL() const { return _unlitFrameDSL; }
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkyboxFrameDSL() const { return _skyboxFrameDSL; }
    [[nodiscard]] const Binding&               getBinding(uint32_t flightIndex) const;

  private:
    IRender* _render = nullptr;
    std::unique_ptr<FrameUploadArena> _uploadArena;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorPool>      _skinningDSP;
    stdptr<IDescriptorSetLayout> _pbrFrameDSL;
    stdptr<IDescriptorPool>      _pbrFrameDSP;
    stdptr<IDescriptorSetLayout> _phongFrameDSL;
    stdptr<IDescriptorPool>      _phongFrameDSP;
    stdptr<IDescriptorSetLayout> _unlitFrameDSL;
    stdptr<IDescriptorPool>      _unlitFrameDSP;
    stdptr<IDescriptorSetLayout> _skyboxFrameDSL;
    stdptr<IDescriptorPool>      _skyboxFrameDSP;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    uint32_t _skinningCapacity = 0;

    bool ensureSkinningCapacity(uint32_t paletteCount);
    void updatePBRFrameDescriptorSet(uint32_t flightIndex,
                                     const FrameUploadArena::Allocation& frame,
                                     const FrameUploadArena::Allocation& light);
    void updatePhongFrameDescriptorSet(uint32_t flightIndex,
                                       const FrameUploadArena::Allocation& frame,
                                       const FrameUploadArena::Allocation& light,
                                       const FrameUploadArena::Allocation& debug);
    void updateUnlitFrameDescriptorSet(uint32_t flightIndex,
                                       const FrameUploadArena::Allocation& frame);
    void updateSkyboxFrameDescriptorSet(uint32_t flightIndex,
                                        const FrameUploadArena::Allocation& frame);
};

} // namespace ya
