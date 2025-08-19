
#include "Render.h"
#include "Platform/Render/Vulkan/VulkanRender.h"



namespace EVertexAttributeFormat
{

std::size_t T2Size(T type)
{
    switch (type) {
    case Float2:
        return sizeof(float) * 2;
    case Float3:
        return sizeof(float) * 3;
    case Float4:
        return sizeof(float) * 4;
    default:
        NE_CORE_ASSERT(false, "Invalid vertex attribute format {}", int(type));
        return 0;
    }
}
}; // namespace EVertexAttributeFormat

IRender *IRender::create(const RenderCreateInfo &ci)
{

    switch (ci.renderAPI) {
    case ERenderAPI::Vulkan:
    {
        IRender *render = new VulkanRender();
        return render;
    } break;
    case ERenderAPI::None:
    case ERenderAPI::OpenGL:
    case ERenderAPI::DirectX12:
    case ERenderAPI::Metal:
    case ERenderAPI::ENUM_MAX:
        UNREACHABLE();
        break;
    }
    return nullptr;
}
