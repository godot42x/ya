#include "Render3D/Common/SceneFamilyResources.h"

#include "Core/Log.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/RenderFrameData.h"

#include <algorithm>

namespace ya
{

std::optional<uint32_t> calculateSceneFamilySkinningCapacity(
    uint32_t currentCapacity,
    uint32_t paletteCount)
{
    constexpr uint32_t maxPaletteCount =
        std::numeric_limits<uint32_t>::max() / sizeof(RenderSkinningPalette);
    const uint32_t requiredCount = std::max(1u, paletteCount);
    if (requiredCount > maxPaletteCount) {
        return std::nullopt;
    }

    uint32_t nextCapacity = currentCapacity == 0 ? 16u : currentCapacity;
    if (nextCapacity > maxPaletteCount) {
        return std::nullopt;
    }
    while (nextCapacity < requiredCount) {
        if (nextCapacity > maxPaletteCount / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }
    return nextCapacity;
}

SceneFamilyResources* allocateSceneFamilyForView(
    RenderSubmission&                 submission,
    const RenderViewRecordingContext& view)
{
    if (view.task) {
        return submission.allocateSceneFamily(makeSceneViewFamilyKey(*view.task));
    }
    return submission.allocateSceneFamily(SceneViewFamilyKey{});
}

bool prepareSceneFamilySkinning(
    RenderSubmission&                   submission,
    SceneFamilyResources&               family,
    const stdptr<IBuffer>&              skinningBuffer,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout)
{
    if (!submission.isRecording() || !skinningBuffer) {
        return false;
    }

    if (!render || !layout) {
        return true;
    }

    DescriptorSetHandle set = family.skinningDescriptorSet(layout.get());
    if (!set) {
        set = submission.allocateDescriptorSet(layout, 1, EPipelineDescriptorType::StorageBuffer);
        if (!set) {
            YA_CORE_ERROR("Failed to allocate scene-family skinning descriptor set");
            return false;
        }
        family.storeSkinningDescriptorSet(layout.get(), set);
    }

    render->getDescriptorHelper()->updateDescriptorSets(
        {IDescriptorSetHelper::genSingleBufferWrite(
            set,
            0,
            EPipelineDescriptorType::StorageBuffer,
            skinningBuffer.get())},
        {});
    return true;
}

} // namespace ya
