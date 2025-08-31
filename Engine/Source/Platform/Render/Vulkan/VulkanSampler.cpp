#include "VulkanSampler.h"
#include "VulkanRender.h"


VulkanSampler::VulkanSampler(VulkanRender *render, const ya::SamplerCreateInfo &ci)
{
    _render = render;
    using namespace ya;
    _name = ci.name;

    render->createSampler(_name, ci, _handle);
}

VulkanSampler::~VulkanSampler()
{
    _render->removeSampler(_name);
}
