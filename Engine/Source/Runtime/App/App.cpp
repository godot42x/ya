#include "Runtime/App/App.h"

#include "Runtime/App/DebugRenderSystem.h"

namespace ya
{

App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;

ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

App::~App() = default;

void App::onRenderGUI(float dt)
{
    (void)dt;
}

void App::addExtension(std::unique_ptr<IAppExtension> extension)
{
    YA_CORE_ASSERT(extension, "Cannot register a null app extension");
    YA_CORE_ASSERT(!_extensionsAttached, "App extensions must be registered before App::init");
    _extensions.push_back(std::move(extension));
}

void App::attachExtensions()
{
    YA_CORE_ASSERT(!_extensionsAttached, "App extensions are already attached");
    for (const auto& extension : _extensions) {
        extension->onAttach(*this);
    }
    _extensionsAttached = true;
}

void App::detachExtensions()
{
    if (!_extensionsAttached) {
        return;
    }
    for (auto it = _extensions.rbegin(); it != _extensions.rend(); ++it) {
        (*it)->onDetach(*this);
    }
    _extensionsAttached = false;
}

void App::dispatchNativeEvent(const SDL_Event& event)
{
    for (const auto& extension : _extensions) {
        extension->onNativeEvent(*this, event);
    }
}

bool App::dispatchExtensionEvent(const Event& event)
{
    for (const auto& extension : _extensions) {
        if (extension->onEvent(*this, event)) {
            return true;
        }
    }
    return false;
}

void App::tickExtensions(float dt)
{
    for (const auto& extension : _extensions) {
        extension->onLogic(*this, dt);
    }
}

void App::prepareExtensionsForRender(float dt)
{
    for (const auto& extension : _extensions) {
        extension->onBeforeRender(*this, dt);
    }
}

void App::recordExtensionPresentation(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& extension : _extensions) {
        extension->onPresentation(*this, commandBuffer, dt);
    }
}

void App::notifyExtensionsSceneActivated(Scene* scene)
{
    for (const auto& extension : _extensions) {
        extension->onSceneActivated(*this, scene);
    }
}

void App::notifyExtensionsSceneDestroyed(Scene* scene)
{
    for (const auto& extension : _extensions) {
        extension->onSceneDestroyed(*this, scene);
    }
}

IRender* App::getRender() const
{
    return _renderRuntime ? _renderRuntime->getRender() : nullptr;
}

std::shared_ptr<ShaderStorage> App::getShaderStorage() const
{
    return _renderRuntime ? _renderRuntime->getShaderStorage() : nullptr;
}

IRenderPipeline* App::getRenderPipeline() const
{
    return _renderRuntime ? _renderRuntime->getActivePipeline() : nullptr;
}

DebugRenderSystem& App::getDebugRenderSystem() const
{
    YA_CORE_ASSERT(_renderRuntime, "RenderRuntime is not available");
    return _renderRuntime->getDebugRenderSystem();
}

bool App::isShadowMappingEnabled() const
{
    return _renderRuntime && _renderRuntime->isShadowMappingEnabled();
}

bool App::isMirrorRenderingEnabled() const
{
    return _renderRuntime && _renderRuntime->isMirrorRenderingEnabled();
}

bool App::hasMirrorRenderResult() const
{
    return _renderRuntime && _renderRuntime->hasMirrorRenderResult();
}

IImageView* App::getShadowDirectionalDepthIV() const
{
    return _renderRuntime ? _renderRuntime->getShadowDirectionalDepthIV() : nullptr;
}

IImageView* App::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    return _renderRuntime ? _renderRuntime->getShadowPointFaceDepthIV(pointLightIndex, faceIndex) : nullptr;
}

bool App::isPostprocessingEnabled() const
{
    return _renderRuntime && _renderRuntime->isPostprocessingEnabled();
}

} // namespace ya
