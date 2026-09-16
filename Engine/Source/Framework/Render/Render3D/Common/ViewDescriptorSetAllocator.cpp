#include "ViewDescriptorSetAllocator.h"

#include "Core/Log.h"
#include "RHI/Core/DescriptorSet.h"

#include <algorithm>

namespace ya
{

void ViewDescriptorSetAllocator::init(
    IRender*                   render,
    std::string_view           label,
    uint32_t                   descriptorsPerSet,
    uint32_t                   chunkSize,
    EPipelineDescriptorType::T descriptorType)
{
    destroy();
    _render            = render;
    _label             = std::string(label);
    _descriptorsPerSet = std::max(1u, descriptorsPerSet);
    _chunkSize         = std::max(1u, chunkSize);
    _descriptorType    = descriptorType;
    if (!grow()) {
        YA_CORE_ERROR("ViewDescriptorSetAllocator failed to create initial {} pool", _label);
    }
}

void ViewDescriptorSetAllocator::destroy()
{
    _pools.clear();
    _allocatedSets     = 0;
    _render            = nullptr;
    _descriptorsPerSet = 1;
    _chunkSize         = kDefaultChunk;
    _descriptorType    = EPipelineDescriptorType::UniformBuffer;
    _label.clear();
}

bool ViewDescriptorSetAllocator::grow()
{
    if (!_render) {
        return false;
    }

    auto nextPool = IDescriptorPool::create(
        _render,
        DescriptorPoolCreateInfo{
            .label     = _label,
            .maxSets   = _chunkSize,
            .poolSizes = {{
                .type            = _descriptorType,
                .descriptorCount = _chunkSize * _descriptorsPerSet,
            }},
        });
    if (!nextPool) {
        YA_CORE_ERROR("ViewDescriptorSetAllocator failed to grow {} descriptor pool", _label);
        return false;
    }
    _pools.push_back(std::move(nextPool));
    return true;
}

DescriptorSetHandle ViewDescriptorSetAllocator::allocate(const stdptr<IDescriptorSetLayout>& layout)
{
    if (!_render || !layout) {
        return {};
    }

    const uint32_t capacity = _chunkSize * static_cast<uint32_t>(_pools.size());
    if (_pools.empty() || _allocatedSets >= capacity) {
        if (!grow()) {
            return {};
        }
    }

    DescriptorSetHandle set = _pools.back()->allocateDescriptorSets(layout);
    if (set) {
        ++_allocatedSets;
    }
    return set;
}

} // namespace ya
