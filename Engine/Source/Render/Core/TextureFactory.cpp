#include "TextureFactory.h"

#include "Platform/Render/Vulkan/VulkanTextureFactory.h"
#include "Render/Render.h"
#include "Runtime/App/App.h"


namespace ya
{

ITextureFactory* ITextureFactory::get()
{

    auto api = App::get()->getRender()->getAPI();
    switch (api) {

    case ERenderAPI::Vulkan:
    {
        static auto vkInst = []() {
            auto render = App::get()->getRender()->as<VulkanRender>();
            return new VulkanTextureFactory(render);
        }();
        return vkInst;
    } break;

    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        UNIMPLEMENTED();
        break;
    }

    return nullptr;
}

} // namespace ya