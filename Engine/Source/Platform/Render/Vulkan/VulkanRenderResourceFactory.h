#pragma once

#include "Render/Core/RenderResourceFactory.h"

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
};

} // namespace ya
