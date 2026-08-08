#pragma once

#include "Foundation/RHI/Core/RenderResourceFactory.h"

namespace ya
{

struct VulkanRender;

class VulkanRenderResourceFactory final : public IRenderResourceFactory
{
    VulkanRender* _render = nullptr;

  public:
    explicit VulkanRenderResourceFactory(VulkanRender* render) : _render(render) {}

    std::shared_ptr<IBuffer> createBuffer(const BufferCreateInfo& desc) override;
    std::shared_ptr<Sampler> createSampler(const SamplerDesc& desc) override;
    std::shared_ptr<IImage> createImage(const ImageCreateInfo& desc) override;
    std::shared_ptr<IImage> importImage(const ImportedImageDesc& desc) override;
    std::shared_ptr<IImageView> createImageView(
        std::shared_ptr<IImage> image,
        const ImageViewCreateInfo& desc) override;
};

} // namespace ya
