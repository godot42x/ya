#include "Runtime/Application/App.h"
#include "Runtime/Application/AppRenderState.h"
#include "Runtime/Application/Automation/AppAutomationControlService.h"
#include "Runtime/Application/WindowManager.h"

#include "Core/Module/ProjectDescriptor.h"
#include "Core/Profiling/Profiling.h"
#include "Core/System/VirtualFileSystem.h"
#include "Runtime/Rendering/Common/UISceneRenderer.h"
#include "Scene/Scene.h"
#include "Runtime/Rendering/Services/DebugRenderSystem.h"

namespace ya
{

App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;

ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

App::App()
    : _renderState(std::make_unique<AppRenderState>())
    , _renderServices(_renderState.get())
    , _sceneServices(this)
    , _automationControlService(std::make_unique<AppAutomationControlService>())
    , gameInputNode(inputManager)
{
    inputRouter.setApp(*this);
    inputRouter.setDefaultNode(gameInputNode);
}

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
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        slot.module->onConfigure(*this, _ci);
    }
}

void App::applyProjectDescriptor(const FProjectDescriptor& descriptor)
{
    YA_PROFILE_FUNCTION();

    _ci.projectPath      = descriptor.sourcePath.string();
    _ci.projectRoot      = descriptor.sourcePath.parent_path().string();
    _ci.defaultScenePath = descriptor.defaultScene;
    inputManager.configureActionBindings(descriptor.inputActions);

    if (_ci.projectRoot) {
        VirtualFileSystem::get()->setGameRoot(*_ci.projectRoot);
    }
}

void App::attachModules()
{
    YA_PROFILE_FUNCTION();

    YA_CORE_ASSERT(!_modulesAttached, "Modules are already attached");
    for (const auto& slot : _modules) {
        slot.module->onAttach(*this);
    }
    _modulesAttached = true;
}

void App::detachModules()
{
    YA_PROFILE_FUNCTION();

    if (!_modulesAttached) {
        return;
    }
    for (auto it = _modules.rbegin(); it != _modules.rend(); ++it) {
        it->module->onDetach(*this);
    }
    _modulesAttached = false;
}

bool App::dispatchHostModuleEvent(const Event& event)
{
    for (const auto& slot : _modules) {
        if (slot.module->onEvent(*this, event)) {
            return true;
        }
    }
    return false;
}

bool App::dispatchInputModuleEvent(const Event& event)
{
    for (const auto& slot : _modules) {
        if (slot.module->onEvent(*this, event)) {
            return true;
        }
    }
    return false;
}

bool App::dispatchInputFallbackEvent(const Event& event)
{
    if (event.getEventType() == EEvent::KeyReleased) {
        const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
        if (keyEvent.getKeyCode() == EKey::Escape) {
            requestQuit();
            return true;
        }
    }

    if (event.getEventType() == EEvent::MouseButtonReleased && _appMode == AppMode::Drawing) {
        const auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (mouseEvent.GetMouseButton() == EMouse::Left) {
            clicked.push_back(_lastMousePos);
            return true;
        }
    }

    // Game-UI (Node2D) picking no longer lives here: it is dispatched by the
    // input node chain (GameInputNode / EditorInputNode) so that an exclusive
    // UI hit can keep the event away from gameplay.
    return false;
}

void App::setInputMode(EInputMode mode)
{
    _inputModeStack.clear();
    _inputMode = mode;
    inputRouter.applyInputMode(mode);
}

void App::pushInputMode(EInputMode mode)
{
    _inputModeStack.push_back(_inputMode);
    _inputMode = mode;
    inputRouter.applyInputMode(mode);
}

void App::popInputMode()
{
    if (_inputModeStack.empty()) {
        YA_CORE_WARN("App::popInputMode: mode stack is empty");
        return;
    }
    _inputMode = _inputModeStack.back();
    _inputModeStack.pop_back();
    inputRouter.applyInputMode(_inputMode);
}

EUIRouteResult App::dispatchUIInputEvent(const Event& event)
{
    if (isStopped() || _inputMode == EInputMode::GameOnly) {
        return EUIRouteResult::NotHandled;
    }

    const EEvent::T eventType = event.getEventType();
    if (eventType != EEvent::MouseButtonPressed &&
        eventType != EEvent::MouseButtonReleased &&
        eventType != EEvent::MouseMoved) {
        return EUIRouteResult::NotHandled;
    }

    // While the game holds the mouse (relative capture) it owns all input:
    // game-UI picking is suspended, matching the editor layout lock.
    if (inputRouter.isMouseCaptured()) {
        return EUIRouteResult::NotHandled;
    }

    Rect2D viewportRect = _renderState && _renderState->runtime
                            ? _renderState->runtime->getViewportRect()
                            : Rect2D{};
    const bool bInViewport = FUIHelper::isPointInRect(_lastMousePos, viewportRect.pos, viewportRect.extent);
    if (!bInViewport) {
        return EUIRouteResult::NotHandled;
    }

    UIAppCtx ctx{
        .lastMousePos = _lastMousePos,
        .bInViewport  = bInViewport,
        .viewportRect = viewportRect,
    };
    Scene* scene = getSceneServices().getActiveScene();
    return UISceneRenderer::handleEvent(event, ctx, scene ? scene->getRootNode() : nullptr);
}

void App::tickModules(float dt)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        slot.module->onLogic(*this, dt);
    }
}

void App::prepareModulesForRender(float dt)
{
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        slot.module->onBeforeRender(*this, dt);
    }
}

void App::recordModuleViewportCompose(ICommandBuffer& commandBuffer, float dt)
{
    for (const auto& slot : _modules) {
        slot.module->onViewportCompose(*this, commandBuffer, dt);
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
    YA_PROFILE_FUNCTION();

    for (const auto& slot : _modules) {
        if (!slot.module->onBeforeAppStateChange(*this, _appState, nextState)) {
            return false;
        }
    }
    return true;
}

void App::notifyModulesAfterAppStateChange(AppState previousState)
{
    YA_PROFILE_FUNCTION();

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

uint64_t App::getElapsedTimeMS() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - _startTime).count();
}

void* App::queryModuleInterface(FInterfaceId interfaceId) const
{
    for (const auto& slot : _modules) {
        if (void* iface = slot.module->queryInterface(interfaceId)) {
            return iface;
        }
    }
    return nullptr;
}

bool App::openProject(const FProjectDescriptor& descriptor)
{
    YA_PROFILE_FUNCTION();

    applyProjectDescriptor(descriptor);

    if (descriptor.defaultScene && !descriptor.defaultScene->empty()) {
        return _sceneServices.loadScene(*descriptor.defaultScene);
    }

    return true;
}

} // namespace ya
