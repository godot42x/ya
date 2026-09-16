#pragma once

#include "Core/Common/Types.h"
#include "Core/Log.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Render.h"

#include <string>
#include <string_view>

namespace ya
{

/**
 * Device-lifetime skinning descriptor layout shared by Forward/Deferred/Shadow
 * resource sets. Instance buffers and descriptor sets live on
 * `SceneFamilyResources` (owned by `RenderSubmission`), not on a per-flight
 * slot of this object.
 */
class PerFlightFrameResourceSetBase
{
  protected:
    PerFlightFrameResourceSetBase()  = default;
    ~PerFlightFrameResourceSetBase() = default;

    PerFlightFrameResourceSetBase(const PerFlightFrameResourceSetBase&)            = delete;
    PerFlightFrameResourceSetBase& operator=(const PerFlightFrameResourceSetBase&) = delete;

    void initSkinningLayout(IRender*         render,
                            std::string_view pipelineName,
                            std::string_view skinningDSLLabel,
                            int32_t          skinningDSLSet);
    void destroySkinningLayout();

    IRender*                     _render = nullptr;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    std::string                  _resourceTag;

  public:
    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }
};

inline void PerFlightFrameResourceSetBase::initSkinningLayout(
    IRender*         render,
    std::string_view pipelineName,
    std::string_view skinningDSLLabel,
    int32_t          skinningDSLSet)
{
    YA_CORE_ASSERT(render != nullptr, "PerFlightFrameResourceSetBase requires a render backend");
    if (render->getResourceFactory() == nullptr) {
        YA_CORE_ASSERT(false, "PerFlightFrameResourceSetBase requires a resource factory");
        return;
    }

    _render      = render;
    _resourceTag = std::string(pipelineName);

    _skinningDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = std::string(skinningDSLLabel),
            .set      = skinningDSLSet,
            .bindings = {{.binding        = 0,
                          .descriptorType = EPipelineDescriptorType::StorageBuffer,
                          .descriptorCount = 1,
                          .stageFlags     = EShaderStage::Vertex}},
        });
    YA_CORE_ASSERT(_skinningDSL != nullptr,
                   "{}FrameResourceSet failed to create skinning descriptor layout",
                   _resourceTag);
}

inline void PerFlightFrameResourceSetBase::destroySkinningLayout()
{
    _skinningDSL.reset();
    _render = nullptr;
    _resourceTag.clear();
}

} // namespace ya
