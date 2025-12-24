#include "IRenderTarget.h"
#include "Core/App/App.h"


// Platform-specific includes
#if USE_VULKAN
    #include "Platform/Render/Vulkan/VulkanRenderTarget.h"
#endif

namespace ya
{


std::shared_ptr<IRenderTarget> createRenderTarget(RenderTargetCreateInfo ci)
{

    auto api = App::get()->currentRenderAPI;
    switch (api) {
    case ERenderAPI::Vulkan:
    {
        return makeShared<VulkanRenderTarget>(ci);
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

void IRenderTarget::onRenderGUI()
{
    // if (ImGui::CollapsingHeader(label.c_str())) {
    //     ImGui::PushID(this);
    //     ImGui::Text("Extent: %d x %d", _extent.width, _extent.height);
    //     ImGui::ColorEdit4("Clear Color", &colorClearValue.color.r);
    //     ImGui::DragFloat("Clear Depth", &depthClearValue.depthStencil.depth, 0.01f, 0.0f, 1.0f);
    //     ImGui::DragInt("Clear Stencil", (int *)&depthClearValue.depthStencil.stencil, 1, 0, 255);
    //     ImGui::PopID();
    // }
}

} // namespace ya
