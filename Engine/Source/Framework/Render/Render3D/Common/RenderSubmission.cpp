#include "Render3D/Common/RenderSubmission.h"

#include "Core/Log.h"
#include "RHI/Render.h"

#include <algorithm>
#include <format>

namespace ya
{

std::optional<FrameUploadArena::Allocation> RenderSubmission::allocateUpload(
    uint32_t size,
    uint32_t alignment)
{
    if (!isRecording() || !_arena || alignment == 0 || size == 0) {
        return std::nullopt;
    }
    return _arena->allocate(_flightIndex, size, alignment);
}

DescriptorSetHandle RenderSubmission::allocateDescriptorSet(
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            descriptorsPerSet,
    EPipelineDescriptorType::T          type)
{
    if (!isRecording() || !_pool) {
        return {};
    }
    return _pool->allocateDescriptorSet(*this, layout, descriptorsPerSet, type);
}

bool RenderSubmission::retain(RetainedResource resource)
{
    if (!isRecording() || !resource) {
        return false;
    }
    _keepalives.push_back(std::move(resource));
    return true;
}

IRenderResourceFactory* RenderSubmission::resourceFactory() const
{
    return _pool ? _pool->resourceFactory() : nullptr;
}

bool RenderSubmission::finish()
{
    if (!isRecording()) {
        return false;
    }
    _finished = true;
    return true;
}

SceneFamilyResources* RenderSubmission::allocateSceneFamily(
    const SceneViewFamilyKey& key,
    const SceneSnapshot* snapshot)
{
    if (!isRecording()) {
        return nullptr;
    }
    for (auto& family : _families) {
        if (family && family->key() == key) {
            family->bindSnapshot(snapshot);
            return family.get();
        }
    }
    _families.push_back(std::make_unique<SceneFamilyResources>(key, snapshot));
    return _families.back().get();
}

SceneFamilyResources* RenderSubmission::findSceneFamily(const SceneViewFamilyKey& key)
{
    return const_cast<SceneFamilyResources*>(
        static_cast<const RenderSubmission*>(this)->findSceneFamily(key));
}

const SceneFamilyResources* RenderSubmission::findSceneFamily(const SceneViewFamilyKey& key) const
{
    for (const auto& family : _families) {
        if (family && family->key() == key) {
            return family.get();
        }
    }
    return nullptr;
}

SceneFamilyResources* RenderSubmission::sceneFamilyAt(uint32_t index)
{
    return const_cast<SceneFamilyResources*>(
        static_cast<const RenderSubmission*>(this)->sceneFamilyAt(index));
}

const SceneFamilyResources* RenderSubmission::sceneFamilyAt(uint32_t index) const
{
    if (index >= _families.size()) {
        return nullptr;
    }
    return _families[index].get();
}

bool RenderSubmissionPool::init(IRender* render)
{
    if (!render || !render->getResourceFactory()) {
        return false;
    }
    return init(*render->getResourceFactory(), render);
}

bool RenderSubmissionPool::init(IRenderResourceFactory& factory, IRender* render)
{
    destroy();
    _render  = render;
    _factory = &factory;
    _arena  = std::make_unique<FrameUploadArena>(
        factory,
        MAX_FLIGHTS_IN_FLIGHT,
        64u * 1024u,
        EBufferUsage::UniformBuffer,
        "RenderSubmission.Upload");
    for (uint32_t flightIndex = 0; flightIndex < MAX_FLIGHTS_IN_FLIGHT; ++flightIndex) {
        RenderSubmission& flight = _flights[flightIndex];
        flight._flightIndex      = flightIndex;
        flight._arena            = _arena.get();
        flight._pool             = this;
    }
    return true;
}

void RenderSubmissionPool::destroy()
{
    for (auto& lane : _descriptorLanes) {
        lane.allocator.destroy();
    }
    _descriptorLanes.clear();
    for (auto& flight : _flights) {
        flight._keepalives.clear();
        flight._families.clear();
        flight._cmdBuf      = nullptr;
        flight._hostSurface = nullptr;
        flight._occupied    = false;
        flight._finished    = false;
        flight._frameToken  = 0;
        flight._arena       = nullptr;
        flight._pool        = nullptr;
    }
    _arena.reset();
    _factory = nullptr;
    _render  = nullptr;
}

RenderSubmission* RenderSubmissionPool::acquire(
    uint32_t               flightIndex,
    uint64_t               frameToken,
    ICommandBuffer*        cmdBuf,
    IRenderSurfaceContext* hostSurface)
{
    if (!_arena || flightIndex >= MAX_FLIGHTS_IN_FLIGHT || !cmdBuf) {
        return nullptr;
    }

    RenderSubmission& flight = _flights[flightIndex];
    if (flight._occupied && flight._frameToken == frameToken) {
        if (flight._finished) {
            return nullptr;
        }
        flight._cmdBuf      = cmdBuf;
        flight._hostSurface = hostSurface;
        return &flight;
    }

    flight._keepalives.clear();
    flight._families.clear();
    flight._frameToken  = frameToken;
    flight._flightIndex = flightIndex;
    flight._cmdBuf      = cmdBuf;
    flight._hostSurface = hostSurface;
    flight._arena       = _arena.get();
    flight._pool        = this;
    flight._occupied    = true;
    flight._finished    = false;
    if (!_arena->beginFlight(flightIndex, frameToken)) {
        flight._occupied = false;
        flight._cmdBuf   = nullptr;
        return nullptr;
    }
    return &flight;
}

RenderSubmission* RenderSubmissionPool::get(uint32_t flightIndex)
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return nullptr;
    }
    RenderSubmission& flight = _flights[flightIndex];
    return flight._occupied ? &flight : nullptr;
}

const RenderSubmission* RenderSubmissionPool::get(uint32_t flightIndex) const
{
    if (flightIndex >= MAX_FLIGHTS_IN_FLIGHT) {
        return nullptr;
    }
    const RenderSubmission& flight = _flights[flightIndex];
    return flight._occupied ? &flight : nullptr;
}

DescriptorSetHandle RenderSubmissionPool::allocateDescriptorSet(
    RenderSubmission&                   submission,
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            descriptorsPerSet,
    EPipelineDescriptorType::T          type)
{
    if (!submission.isRecording() || !_render || !layout) {
        return {};
    }

    const uint32_t count = std::max(1u, descriptorsPerSet);
    for (auto& lane : _descriptorLanes) {
        if (lane.type == type && lane.descriptorsPerSet == count) {
            return lane.allocator.allocate(layout);
        }
    }

    DescriptorLane lane{};
    lane.type              = type;
    lane.descriptorsPerSet = count;
    lane.allocator.init(
        _render,
        std::format("RenderSubmission_DS_{}_{}", static_cast<uint32_t>(type), count),
        count,
        ViewDescriptorSetAllocator::kDefaultChunk,
        type);
    _descriptorLanes.push_back(std::move(lane));
    return _descriptorLanes.back().allocator.allocate(layout);
}

} // namespace ya
