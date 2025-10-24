#include "Sampler.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanSampler.h"

namespace ya
{


stdptr<Sampler> Sampler::create(const SamplerDesc &desc)
{
    auto api = App::get()->getRender()->getAPI();
    switch (api) {
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
        break;
    case ERenderAPI::Vulkan:
        return makeShared<VulkanSampler>(desc);
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        break;
    }
    UNREACHABLE();
    return nullptr;
}

} // namespace ya
