#include "ViewPassResources.h"

#include "RHI/Render.h"
#include "Render3D/Common/FrameResourceSubmission.h"

#include <algorithm>

namespace ya
{

bool writeUniformPassBinding(
    RenderSubmission&                   submission,
    IRender*                            render,
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            alignment,
    const void*                         data,
    uint32_t                            size,
    UniformBufferPassBinding&           out)
{
    if (!submission.isRecording() || alignment == 0 || data == nullptr || size == 0) {
        return false;
    }

    auto slice = writeUploadSlice(submission, alignment, data, size);
    if (!slice) {
        return false;
    }
    out.slice = *slice;

    if (layout) {
        out.set = submission.allocateDescriptorSet(layout, 1, EPipelineDescriptorType::UniformBuffer);
    }
    if (render && out.set) {
        render->getDescriptorHelper()->updateDescriptorSets({
            IDescriptorSetHelper::genBufferWrite(
                out.set,
                0,
                0,
                EPipelineDescriptorType::UniformBuffer,
                {out.slice.descriptor()}),
        });
    }
    return true;
}

DescriptorSetHandle allocateCombinedImageSamplerSet(
    RenderSubmission&                   submission,
    const stdptr<IDescriptorSetLayout>& layout,
    uint32_t                            descriptorsPerSet)
{
    if (!layout) {
        return {};
    }
    return submission.allocateDescriptorSet(
        layout,
        std::max(1u, descriptorsPerSet),
        EPipelineDescriptorType::CombinedImageSampler);
}

void allocateBloomPassBindings(
    RenderSubmission&                   submission,
    const stdptr<IDescriptorSetLayout>& extractLayout,
    const stdptr<IDescriptorSetLayout>& blurLayout,
    const stdptr<IDescriptorSetLayout>& compositeLayout,
    BloomPassBindings&                  out)
{
    out.extract.set = allocateCombinedImageSamplerSet(submission, extractLayout, 1);
    for (auto& pass : out.blur) {
        pass.set = allocateCombinedImageSamplerSet(submission, blurLayout, 1);
    }
    out.composite.set = allocateCombinedImageSamplerSet(submission, compositeLayout, 2);
}

} // namespace ya
