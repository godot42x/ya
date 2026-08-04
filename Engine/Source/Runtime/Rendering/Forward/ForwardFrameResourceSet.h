#pragma once

#include "Core/Common/Types.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Stage/IRenderStage.h"

#include <array>

namespace ya
{

struct IBuffer;
struct IRender;

/**
 * Owns Forward's shared per-flight skinning storage and descriptor sets.
 *
 * The descriptor set layout is a pipeline resource; the storage buffers are
 * capacity-managed per-flight CPU-to-GPU buffers that are replaced (and retired
 * through DeferredDeletionQueue) when the palette count grows. Stages only
 * borrow the current flight's descriptor set from the pipeline-level resource
 * set, matching the Deferred path (FG-205/FG-503).
 */
class ForwardFrameResourceSet
{
  public:
    struct Binding
    {
        DescriptorSetHandle skinningDescriptorSet{};
        stdptr<IBuffer>     skinningBuffer;

        [[nodiscard]] bool isValid() const
        {
            return skinningDescriptorSet && skinningBuffer;
        }
    };

    void init(IRender* render);
    void destroy();

    /** Upload the current frame's skinning palettes for the fence-safe flight. */
    bool prepareSkinning(const RenderStageContext& ctx);

    [[nodiscard]] stdptr<IDescriptorSetLayout> getSkinningDSL() const { return _skinningDSL; }
    [[nodiscard]] const Binding&               getBinding(uint32_t flightIndex) const;

  private:
    IRender* _render = nullptr;
    stdptr<IDescriptorSetLayout> _skinningDSL;
    stdptr<IDescriptorPool>      _skinningDSP;
    std::array<Binding, MAX_FLIGHTS_IN_FLIGHT> _bindings{};
    uint32_t _skinningCapacity = 0;

    bool ensureSkinningCapacity(uint32_t paletteCount);
};

} // namespace ya
