#include "SceneFamilyResources.h"

#include "Core/Log.h"
#include "RHI/Core/DescriptorSet.h"
#include "RHI/Core/RenderResourceFactory.h"
#include "RHI/Render.h"
#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmission.h"
#include "Render3D/RenderFrameData.h"

#include <algorithm>
#include <format>

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
    const SceneFrameSnapshot* snapshot = nullptr;
    if (view.frameData) {
        snapshot = view.frameData->sceneSnapshot.get();
    }
    if (view.task) {
        return submission.allocateSceneFamily(makeSceneViewFamilyKey(*view.task), snapshot);
    }
    return submission.allocateSceneFamily(SceneViewFamilyKey{}, snapshot);
}

bool prepareSceneFamilySkinning(
    RenderSubmission&                   submission,
    SceneFamilyResources&               family,
    IRenderResourceFactory&             factory,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout,
    std::string_view                    label)
{
    if (!submission.isRecording()) {
        return false;
    }

    const std::vector<RenderSkinningPalette>* palettes = nullptr;
    if (family.snapshot()) {
        palettes = &family.snapshot()->skinningPalettes;
        if (palettes->size() > std::numeric_limits<uint32_t>::max()) {
            YA_CORE_ERROR("{} skinning palette count exceeds uint32 range", label);
            return false;
        }
    }
    const uint32_t paletteCount = palettes ? static_cast<uint32_t>(palettes->size()) : 0u;

    SceneFamilyGpuPacket& gpu = family.gpu();
    if (!gpu.skinningBuffer || gpu.skinningCapacity < std::max(1u, paletteCount)) {
        const auto nextCapacity = calculateSceneFamilySkinningCapacity(gpu.skinningCapacity, paletteCount);
        if (!nextCapacity.has_value()) {
            YA_CORE_ERROR("{} skinning palette count {} exceeds buffer size limit", label, paletteCount);
            return false;
        }

        auto nextBuffer = factory.createBuffer(BufferCreateInfo{
            .label       = std::format("{}_Skinning_SSBO_family", label),
            .usage       = EBufferUsage::StorageBuffer,
            .size        = static_cast<uint32_t>(*nextCapacity * sizeof(RenderSkinningPalette)),
            .memoryUsage = EMemoryUsage::CpuToGpu,
        });
        if (!nextBuffer) {
            YA_CORE_ERROR("{} failed to create scene-family skinning buffer", label);
            return false;
        }

        if (gpu.skinningBuffer) {
            submission.retain(gpu.skinningBuffer);
        }
        gpu.skinningBuffer     = std::move(nextBuffer);
        gpu.skinningCapacity   = *nextCapacity;
        gpu.skinningUploaded   = false;
        submission.retain(gpu.skinningBuffer);
    }

    gpu.skinningPaletteCount = paletteCount;
    if (!gpu.skinningUploaded && palettes && !palettes->empty()) {
        const uint32_t byteCount = paletteCount * sizeof(RenderSkinningPalette);
        if (!gpu.skinningBuffer->writeData(palettes->data(), byteCount, 0) ||
            !gpu.skinningBuffer->flush(byteCount, 0)) {
            YA_CORE_ERROR("{} failed to upload scene-family skinning palettes", label);
            return false;
        }
    }
    gpu.skinningUploaded = true;

    if (!render || !layout) {
        return true;
    }

    DescriptorSetHandle set = family.skinningDescriptorSet(layout.get());
    if (!set) {
        set = submission.allocateDescriptorSet(layout, 1, EPipelineDescriptorType::StorageBuffer);
        if (!set) {
            YA_CORE_ERROR("{} failed to allocate scene-family skinning descriptor set", label);
            return false;
        }
        family.storeSkinningDescriptorSet(layout.get(), set);
    }

    render->getDescriptorHelper()->updateDescriptorSets(
        {IDescriptorSetHelper::genSingleBufferWrite(
            set,
            0,
            EPipelineDescriptorType::StorageBuffer,
            gpu.skinningBuffer.get())},
        {});
    return true;
}

} // namespace ya
