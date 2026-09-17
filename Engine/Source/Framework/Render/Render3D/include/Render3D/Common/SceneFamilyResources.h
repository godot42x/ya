#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "Render3D/Common/SceneRenderScheduler.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace ya
{

struct IDescriptorSetLayout;
struct IRender;
struct IRenderResourceFactory;
class RenderSubmission;
struct RenderViewRecordingContext;
struct SceneSnapshot;

/// GPU packet shared by every View in one Scene family.
/// Skinning SSBO is family-owned; descriptor sets are per pipeline layout
/// wrapping that same buffer.
struct SceneFamilyGpuPacket
{
    stdptr<IBuffer> skinningBuffer;
    uint32_t        skinningPaletteCount = 0;
    uint32_t        skinningCapacity     = 0;
    bool            skinningUploaded     = false;
};

/// Submission-owned Scene family. Views store a pointer; they do not own
/// skinning or the scene GPU packet. Different keys yield different instances
/// even inside the same command buffer.
class SceneFamilyResources
{
    struct SkinningSet
    {
        const IDescriptorSetLayout* layout = nullptr;
        DescriptorSetHandle         set{};
    };

    SceneViewFamilyKey          _key;
    const SceneSnapshot*        _snapshot = nullptr;
    SceneFamilyGpuPacket        _gpu;
    std::vector<SkinningSet>    _skinningSets;

  public:
    explicit SceneFamilyResources(SceneViewFamilyKey key, const SceneSnapshot* snapshot = nullptr)
        : _key(key), _snapshot(snapshot)
    {}

    [[nodiscard]] const SceneViewFamilyKey& key() const { return _key; }
    [[nodiscard]] const SceneSnapshot* snapshot() const { return _snapshot; }
    void bindSnapshot(const SceneSnapshot* snapshot)
    {
        if (!_snapshot) {
            _snapshot = snapshot;
        }
    }

    [[nodiscard]] SceneFamilyGpuPacket&       gpu() { return _gpu; }
    [[nodiscard]] const SceneFamilyGpuPacket& gpu() const { return _gpu; }

    [[nodiscard]] DescriptorSetHandle skinningDescriptorSet(const IDescriptorSetLayout* layout) const
    {
        if (!layout) {
            return {};
        }
        for (const auto& slot : _skinningSets) {
            if (slot.layout == layout) {
                return slot.set;
            }
        }
        return {};
    }

    void storeSkinningDescriptorSet(const IDescriptorSetLayout* layout, DescriptorSetHandle set)
    {
        if (!layout || !set) {
            return;
        }
        for (auto& slot : _skinningSets) {
            if (slot.layout == layout) {
                slot.set = set;
                return;
            }
        }
        _skinningSets.push_back(SkinningSet{.layout = layout, .set = set});
    }
};

[[nodiscard]] std::optional<uint32_t> calculateSceneFamilySkinningCapacity(
    uint32_t currentCapacity,
    uint32_t paletteCount);

[[nodiscard]] SceneFamilyResources* allocateSceneFamilyForView(
    RenderSubmission&                 submission,
    const RenderViewRecordingContext& view);

bool prepareSceneFamilySkinning(
    RenderSubmission&                   submission,
    SceneFamilyResources&               family,
    IRenderResourceFactory&             factory,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout,
    std::string_view                    label);

} // namespace ya
