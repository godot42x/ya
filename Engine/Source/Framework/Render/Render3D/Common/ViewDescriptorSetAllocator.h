#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/DescriptorSet.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct IRender;

/// Growable UniformBuffer descriptor-pool chain for View-owned sets.
///
/// `MAX_FLIGHTS_IN_FLIGHT` is the fence axis, not the View axis. Each View
/// allocates its own descriptor set; the pool grows before allocate so Vulkan
/// cannot assert on pool exhaustion.
class ViewDescriptorSetAllocator
{
  public:
    static constexpr uint32_t kDefaultChunk = 8;

  private:
    IRender*                             _render            = nullptr;
    std::vector<stdptr<IDescriptorPool>> _pools;
    uint32_t                             _allocatedSets     = 0;
    uint32_t                             _descriptorsPerSet = 1;
    uint32_t                             _chunkSize         = kDefaultChunk;
    std::string                          _label;

    bool grow();

  public:
    void init(IRender*        render,
              std::string_view label,
              uint32_t        descriptorsPerSet,
              uint32_t        chunkSize = kDefaultChunk);
    void destroy();

    DescriptorSetHandle allocate(const stdptr<IDescriptorSetLayout>& layout);

    [[nodiscard]] uint32_t allocatedSetCount() const { return _allocatedSets; }
};

} // namespace ya
