#pragma once

#include "Render3D/Common/RenderViewOutput.h"
#include "RHI/Core/RenderTexture.h"
#include "Render3D/Common/RenderPipelineSettings.h"
#include "Render3D/Common/SceneViewDesc.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace ya
{

struct IRenderResourceFactory;

enum class EViewAttachment : uint8_t
{
    SceneColor,
    SceneDepth,
    DisplayColor,
    EntityId,
    GBuffer0,
    GBuffer1,
    GBuffer2,
    GBuffer3,
    SSAO,
    BloomExtract,
    BloomBlur,
    BloomComposite,
    Count,
};

struct ViewAttachmentDesc
{
    EViewAttachment role{};
    EFormat::T       format  = EFormat::Undefined;
    EImageUsage::T   usage   = EImageUsage::None;
    ESampleCount::T  samples = ESampleCount::Sample_1;
    bool             isDepth = false;

    bool operator==(const ViewAttachmentDesc&) const = default;
};

struct ViewTargetRequest
{
    SceneViewId                    viewId = 0;
    ERenderPipelineKind            pipeline{};
    Extent2D                       extent{};
    std::vector<ViewAttachmentDesc> attachments;

    bool operator==(const ViewTargetRequest&) const = default;
};

struct ViewTargetAllocation
{
    ViewTargetRequest desc{};
    uint64_t          generation = 0;
    std::array<std::shared_ptr<RenderTexture>, static_cast<size_t>(EViewAttachment::Count)> attachments{};

    [[nodiscard]] std::shared_ptr<RenderTexture> find(EViewAttachment role) const;
};

struct ViewTargetLease
{
    std::shared_ptr<ViewTargetAllocation> allocation;

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(allocation); }
    [[nodiscard]] SceneViewId viewId() const { return allocation ? allocation->desc.viewId : 0; }
    [[nodiscard]] Extent2D extent() const { return allocation ? allocation->desc.extent : Extent2D{}; }
    [[nodiscard]] std::shared_ptr<RenderTexture> find(EViewAttachment role) const
    {
        return allocation ? allocation->find(role) : nullptr;
    }
};

class YA_RENDER_3D_API ViewTargetStore
{
    struct Entry
    {
        SceneViewKey key;
        std::shared_ptr<ViewTargetAllocation> allocation;
    };

    IRenderResourceFactory*                     _factory = nullptr;
    std::unordered_map<SceneViewId, Entry>      _entries;
    RenderViewOutputTable                       _publications;
    uint64_t                                    _nextGeneration = 1;

  public:
    void init(IRenderResourceFactory& factory);
    void clear();

    /// Register/unregister the logical View lifecycle. Registration names a
    /// View the store may own targets for and creates no GPU resource; the
    /// first target request allocates. A registered View that a frame does not
    /// request keeps its allocation. Unregistering drops the long-term
    /// allocation reference and makes every flight's publication of that View
    /// unqueryable immediately; recorded submissions keep their own keepalive.
    void registerView(SceneViewKey key);
    void unregisterView(SceneViewKey key);

    bool prepare(std::span<const ViewTargetRequest> requests);
    [[nodiscard]] ViewTargetLease lease(SceneViewId viewId) const;
    [[nodiscard]] uint32_t residentAllocationCount() const { return static_cast<uint32_t>(_entries.size()); }

    /// Per-flight publication of successful View outputs. Present, picking and
    /// debug read the same table through these entry points.
    [[nodiscard]] bool beginPublication(uint32_t flightIndex, uint64_t frameToken)
    {
        return _publications.beginSubmission(flightIndex, frameToken);
    }
    [[nodiscard]] const RenderViewOutput* publishView(uint32_t flightIndex, RenderViewOutput output)
    {
        return _publications.publish(flightIndex, std::move(output));
    }
    [[nodiscard]] const RenderViewOutput* findPublication(uint32_t flightIndex, SceneViewId viewId) const
    {
        return _publications.find(flightIndex, viewId);
    }

  private:
    [[nodiscard]] std::shared_ptr<ViewTargetAllocation> createAllocation(const ViewTargetRequest& request);
};

} // namespace ya
