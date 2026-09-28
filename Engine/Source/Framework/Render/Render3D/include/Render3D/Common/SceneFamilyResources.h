#pragma once

#include "Core/Common/Types.h"
#include "RHI/Core/Buffer.h"
#include "RHI/Core/DescriptorSet.h"
#include "Render3D/Common/SceneRenderScheduler.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ya
{

struct IDescriptorSetLayout;
struct IRender;
class RenderSubmission;
struct RenderViewRecordingContext;

/// Submission-owned Scene family. Views store a pointer; skinning buffers are
/// scene-owned (see SceneSkinningCache) and only referenced from here through
/// the View's prepared data. Different keys yield different instances even
/// inside the same command buffer.
class SceneFamilyResources
{
    struct SkinningSet
    {
        const IDescriptorSetLayout* layout = nullptr;
        DescriptorSetHandle         set{};
    };

    SceneViewFamilyKey       _key;
    std::vector<SkinningSet> _skinningSets;

  public:
    explicit SceneFamilyResources(SceneViewFamilyKey key)
        : _key(key)
    {}

    [[nodiscard]] const SceneViewFamilyKey& key() const { return _key; }

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

/**
 * @brief Bind the scene-wide skinning buffer into this family's descriptor set.
 *
 * The buffer itself is owned by the SceneSkinningCache and resolved before the
 * graph is built; the family only owns the per-layout descriptor set wrapping
 * it (allocated from the recording submission, like all submission-owned sets).
 */
bool prepareSceneFamilySkinning(
    RenderSubmission&                   submission,
    SceneFamilyResources&               family,
    const stdptr<IBuffer>&              skinningBuffer,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout);

} // namespace ya
