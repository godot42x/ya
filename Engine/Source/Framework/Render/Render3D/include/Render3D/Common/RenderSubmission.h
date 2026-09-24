#pragma once

#include "Core/Common/RetainedResource.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/FrameUploadArena.h"
#include "RHI/RenderDefines.h"
#include "Render3D/Common/SceneFamilyResources.h"
#include "Render3D/Common/ViewDescriptorSetAllocator.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ya
{

struct ICommandBuffer;
struct IRender;
struct IRenderResourceFactory;
struct SceneSnapshot;
class RenderSubmissionPool;

/// One GPU command submission: command buffer, frame token, upload arena,
/// transient descriptor allocation, keepalive and finish state.
///
/// Persistent pipeline objects do not belong here. A recorded View contributes
/// one allocation keepalive; descriptor and upload slots remain submission-owned.
class RenderSubmission
{
    friend class RenderSubmissionPool;

    uint64_t                         _frameToken  = 0;
    uint32_t                         _flightIndex = 0;
    ICommandBuffer*                  _cmdBuf      = nullptr;
    FrameUploadArena*                _arena       = nullptr;
    RenderSubmissionPool*            _pool        = nullptr;
    std::vector<RetainedResource>    _keepalives;
    std::vector<std::unique_ptr<SceneFamilyResources>> _families;
    bool                             _occupied = false;
    bool                             _finished = false;

  public:
    [[nodiscard]] uint64_t               frameToken() const { return _frameToken; }
    [[nodiscard]] uint32_t               flightIndex() const { return _flightIndex; }
    [[nodiscard]] ICommandBuffer*        commandBuffer() const { return _cmdBuf; }
    [[nodiscard]] IRenderResourceFactory* resourceFactory() const;
    [[nodiscard]] bool                   occupied() const { return _occupied; }
    [[nodiscard]] bool                   isRecording() const { return _occupied && !_finished; }
    [[nodiscard]] bool                   isFinished() const { return _occupied && _finished; }
    [[nodiscard]] const std::vector<RetainedResource>& keepalives() const { return _keepalives; }

    [[nodiscard]] std::optional<FrameUploadArena::Allocation> allocateUpload(
        uint32_t size,
        uint32_t alignment);
    [[nodiscard]] DescriptorSetHandle allocateDescriptorSet(
        const stdptr<IDescriptorSetLayout>& layout,
        uint32_t                            descriptorsPerSet,
        EPipelineDescriptorType::T          type = EPipelineDescriptorType::UniformBuffer);
    bool retain(RetainedResource resource);
    bool finish();

    SceneFamilyResources* allocateSceneFamily(
        const SceneViewFamilyKey&   key,
        const SceneSnapshot*        snapshot = nullptr);
    [[nodiscard]] SceneFamilyResources*       findSceneFamily(const SceneViewFamilyKey& key);
    [[nodiscard]] const SceneFamilyResources* findSceneFamily(const SceneViewFamilyKey& key) const;
    [[nodiscard]] uint32_t                    sceneFamilyCount() const
    {
        return static_cast<uint32_t>(_families.size());
    }
    [[nodiscard]] SceneFamilyResources* sceneFamilyAt(uint32_t index);
    [[nodiscard]] const SceneFamilyResources* sceneFamilyAt(uint32_t index) const;
};

/// Per-flight pool of live submissions. `MAX_FLIGHTS_IN_FLIGHT` is the GPU
/// fence axis. A new token on a flight drops the previous keepalives; the same
/// token is idempotent until finish().
class RenderSubmissionPool
{
    struct DescriptorLane
    {
        EPipelineDescriptorType::T type              = EPipelineDescriptorType::UniformBuffer;
        uint32_t                   descriptorsPerSet = 1;
        ViewDescriptorSetAllocator allocator;
    };

    IRender*                                          _render = nullptr;
    IRenderResourceFactory*                           _factory = nullptr;
    std::unique_ptr<FrameUploadArena>                 _arena;
    std::vector<DescriptorLane>                       _descriptorLanes;
    std::array<RenderSubmission, MAX_FLIGHTS_IN_FLIGHT> _flights{};

  public:
    bool init(IRender* render);
    bool init(IRenderResourceFactory& factory, IRender* render = nullptr);
    void destroy();
    void clear() { destroy(); }

    RenderSubmission* acquire(
        uint32_t        flightIndex,
        uint64_t        frameToken,
        ICommandBuffer* cmdBuf);

    [[nodiscard]] RenderSubmission*       get(uint32_t flightIndex);
    [[nodiscard]] const RenderSubmission* get(uint32_t flightIndex) const;
    [[nodiscard]] IRenderResourceFactory* resourceFactory() const { return _factory; }

    [[nodiscard]] DescriptorSetHandle allocateDescriptorSet(
        RenderSubmission&                   submission,
        const stdptr<IDescriptorSetLayout>& layout,
        uint32_t                            descriptorsPerSet,
        EPipelineDescriptorType::T          type);
};

} // namespace ya
