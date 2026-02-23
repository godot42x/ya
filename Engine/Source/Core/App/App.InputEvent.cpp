#include "App.h"

#include "Core/Debug/RenderDocCapture.h"

namespace ya
{

void App::onSceneViewportResized(Rect2D rect)
{
    _viewportRect     = rect;
    float aspectRatio = rect.extent.x > 0 && rect.extent.y > 0 ? rect.extent.x / rect.extent.y : 16.0f / 9.0f;
    camera.setAspectRatio(aspectRatio);

    Extent2D newExtent{
        .width  = static_cast<uint32_t>(rect.extent.x),
        .height = static_cast<uint32_t>(rect.extent.y),
    };

    _viewportRT->setExtent(newExtent);

    // TODO: this should just be a framebuffer?
    //      but framebuffer depend on the renderpass
    // Recreate postprocess image when viewport size changes
    if (_render && newExtent.width > 0 && newExtent.height > 0) {

        // Wait for GPU to finish using old resources before destroying them
        if (_postprocessTexture) {
            _render->waitIdle();
        }
        _postprocessTexture.reset();
        _postprocessTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "PostprocessRenderTarget",
            .width   = newExtent.width,
            .height  = newExtent.height,
            .format  = EFormat::R8G8B8A8_UNORM,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
        // }
    }
}


bool App::onWindowResized(const WindowResizeEvent& event)
{
    auto  w           = event.GetWidth();
    auto  h           = event.GetHeight();
    float aspectRatio = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.f;
    YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", w, h, aspectRatio);
    // camera.setAspectRatio(aspectRatio);
    _windowSize = {w, h};

    // Notify RenderTargetPool of window resize
    // RenderTargetPool::get().onWindowResized(static_cast<uint32_t>(w), static_cast<uint32_t>(h));

    return false;
}

bool App::onKeyReleased(const KeyReleasedEvent& event)
{
    // if (event.getKeyCode() == EKey::F9) {
    //     if (_renderDocCapture && _renderDocCapture->isAvailable()) {
    //         if (event.isCtrlPressed()) {
    //             _renderDocCapture->requestAfterFrames(120);
    //         }
    //         else {
    //             _renderDocCapture->requestNextFrame();
    //         }
    //         return true;
    //     }
    // }

    if (event.getKeyCode() == EKey::Escape) {
        YA_CORE_INFO("{}", event.toString());
        requestQuit();
        return true;
    }
    return false;
}

bool App::onMouseMoved(const MouseMoveEvent& event)
{
    _lastMousePos = glm::vec2(event.getX(), event.getY());
    return false;
}

bool App::onMouseButtonReleased(const MouseButtonReleasedEvent& event)
{
    // YA_CORE_INFO("Mouse Button Released: {}", event.toString());
    switch (_appMode) {
    case Control:
        break;
    case Drawing:
    {
        // TODO: make a cmdList to async render and draw before Render2D::begin or after Render2D::end
        if (event.GetMouseButton() == EMouse::Left) {
            clicked.push_back(_lastMousePos);
        }
    } break;
    }

    return false;
}

bool App::onMouseScrolled(const MouseScrolledEvent& event)
{
    return false;
}

} // namespace ya