#include "Runtime/App/App.h"

#include "Core/Module/ProjectDescriptor.h"
#include "Core/System/VirtualFileSystem.h"
#include "Runtime/App/DebugRenderSystem.h"

namespace ya
{

App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;

ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

App::~App() = default;

void App::addModule(std::unique_ptr<IModule> module)
{
    YA_CORE_ASSERT(module, "Cannot register a null module");
    YA_CORE_ASSERT(!_modulesAttached, "Modules must be registered before App::init");
    IModule* instance = module.get();
    _modules.push_back({.owned = std::move(module), .module = instance});
}

void App::addModule(IModule& module)
{
    YA_CORE_ASSERT(!_modulesAttached, "Modules must be registered before App::init");
    _modules.push_back({.module = &module});
}

void App::configureModules()
{
    for (const auto& slot : _modules) {
        slot.module->onConfigure(*this, _ci);
    }
}

void App::attachModules()
{
    YA_CORE_ASSERT(!_modulesAttached, "Modules are already attached");
    for (const auto& slot : _modules) {
        slot.module->onAttach(*this);
    }
    _modulesAttached = true;
}

void App::detachModules()
{
    if (!_modulesAttached) {
        return;
    }
    for (auto it = _modules.rbegin(); it != _modules.rend(); ++it) {
        it->module->onDetach(*this);
    }
    _modulesAttached = false;
}

void App::dispatchNativeEvent(const SDL_Event& event)
{
    for (const auto& slot : _modules) {
        slot.module->onNativeEvent(*this, event);
    }
}

bool App::dispatchModuleEvent(const Event& event)
{
    for (const auto& slot : _modules) {
        if (slot.module->onEvent(*this, event)) {
            return true;
        }
    }
    return false;
}

void App::tickModules(float dt)
{
    for (const auto& slot : _modules) {
        slot.module->onLogic(*this, dt);
    }
}

void App::prepareModulesForRender(float dt)
{
    for (const auto& slot : _modules) {
        slot.module->onBeforeRender(*this, dt);
    }
}

void App::recordModuleBeforePresentation(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        slot.module->onBeforePresentation(*this, commandBuffer, dt);
    }
}

void App::recordModulePresentation(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        slot.module->onPresentation(*this, commandBuffer, dt);
    }
}

bool App::notifyModulesBeforeAppStateChange(AppState nextState)
{
    for (const auto& slot : _modules) {
        if (!slot.module->onBeforeAppStateChange(*this, _appState, nextState)) {
            return false;
        }
    }
    return true;
}

void App::notifyModulesAfterAppStateChange(AppState previousState)
{
    for (const auto& slot : _modules) {
        slot.module->onAfterAppStateChange(*this, previousState, _appState);
    }
}

void App::notifyModulesSceneActivated(Scene* scene)
{
    for (const auto& slot : _modules) {
        slot.module->onSceneActivated(*this, scene);
    }
}

void App::notifyModulesSceneDestroyed(Scene* scene)
{
    for (const auto& slot : _modules) {
        slot.module->onSceneDestroyed(*this, scene);
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

bool App::openProject(const FProjectDescriptor& descriptor)
{
    _ci.projectPath      = descriptor.sourcePath.string();
    _ci.projectRoot      = descriptor.sourcePath.parent_path().string();
    _ci.defaultScenePath = descriptor.defaultScene;
    inputManager.configureActionBindings(descriptor.inputActions);

    if (_ci.projectRoot) {
        VirtualFileSystem::get()->setGameRoot(*_ci.projectRoot);
    }

    if (descriptor.defaultScene && !descriptor.defaultScene->empty()) {
        return loadScene(*descriptor.defaultScene);
    }

    return true;
}

} // namespace ya
