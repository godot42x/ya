#include "VulkanSampler.h"
#include "VulkanRender.h"
#include "core/App/App.h"


VulkanSampler::VulkanSampler(const ya::SamplerCreateInfo &ci)
{
    _render = static_cast<VulkanRender *>(ya::App::get()->getRender());
    using namespace ya;
    _label = ci.label;

    _handle = _render->createSampler(ci);
    YA_CORE_ASSERT(_handle != VK_NULL_HANDLE, "Failed to create sampler");
}

VulkanSampler::~VulkanSampler()
{
    _render->removeSampler(_label);
}
