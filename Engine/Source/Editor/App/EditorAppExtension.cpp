#include "Editor/App/EditorAppExtension.h"

#include "Editor/App/EditorLayer.h"
#include "Editor/App/EditorPlaySession.h"
#include "Editor/App/EditorRuntimeToolsWindow.h"

#include "Core/Camera/FreeCameraController.h"
#include "Editor/EditorProfilingSettings.h"
#include "Editor/Inspector/TypeRenderer.h"
#include "Config/ConfigManager.h"
#include "Core/Profiling/Profiling.h"
#include "Editor/ImGui/ImGuiHelper.h"
#include "Runtime/App/App.h"
#include "Runtime/App/RenderRuntime.h"
#include "Runtime/App/Common/Shadow/Common/ShadowSettingsConfig.h"

#include <string_view>

namespace ya
{

namespace
{

EditorLayer* gEditorLayer = nullptr;

glm::vec3 resolveInitialEditorCameraPosition(const App& app)
{
    return app.getDesc().automation.editorCameraPosition.value_or(glm::vec3(0.0f, 0.0f, 5.0f));
}

glm::vec3 resolveInitialEditorCameraRotation(const App& app)
{
    return app.getDesc().automation.editorCameraRotation.value_or(glm::vec3(0.0f, 0.0f, 0.0f));
}

void initializeEditorCamera(App& app, EditorLayer& layer)
{
    auto& editorCamera = layer.getCamera();
    const auto& desc   = app.getDesc();
    const float aspect = desc.height > 0 ? static_cast<float>(desc.width) / static_cast<float>(desc.height) : (16.0f / 9.0f);
    editorCamera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
    editorCamera.setPositionAndRotation(resolveInitialEditorCameraPosition(app), resolveInitialEditorCameraRotation(app));
}


class EditorAppExtension final : public IAppExtension
{
  public:
    void onConfigure(App& app, AppDesc& desc) override
    {
        ConfigManager::get().openDocument("editor", "Engine/Saved/Config/Editor.json");
        migrateLegacyRuntimeSettings();
        if (!shadow_settings::hasRuntimeSettings()) {
            shadow_settings::saveRuntimeSettings(
                shadow_settings::loadSettingsFromDocument("editor", app.getShadowSettings()));
        }
        editor_profiling_settings::load();
        if (!desc.defaultScenePath) {
            const std::string path = ConfigManager::get().getOr<std::string>("editor", "startup.defaultScenePath", "");
            if (!path.empty()) {
                desc.defaultScenePath = path;
            }
        }
    }

    void onAttach(App& app) override
    {
        auto* renderRuntime = app.getRenderRuntime();
        YA_CORE_ASSERT(renderRuntime, "Editor extension requires an initialized RenderRuntime");

        ImGuiManager::get().init(renderRuntime->getRender(), nullptr);
        registerBuiltinTypeRenderers();

        _layer = std::make_unique<EditorLayer>(&app);
        initializeEditorCamera(app, *_layer);
        _layer->onAttach();
        gEditorLayer = _layer.get();
    }

    void onDetach(App& app) override
    {
        _playSession.shutdown(app);
        app.clearExtensionRenderFrameState();
        if (_layer) {
            _layer->onDetach();
            _layer.reset();
        }
        gEditorLayer = nullptr;
        ImGuiManager::get().shutdown();
    }

    bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState) override
    {
        if (previousState == AppState::Stopped && nextState != AppState::Stopped) {
            return _playSession.begin(app, nextState);
        }
        if (previousState != AppState::Stopped && nextState == AppState::Stopped) {
            _playSession.end(app);
        }
        return true;
    }

    void onSceneActivated(App& app, Scene* scene) override
    {
        _playSession.onSceneActivated(app, scene);
        if (!_layer) {
            return;
        }

        const uint64_t selectedUUID = _layer->getSelectedEntityUUID();
        _layer->setEditableScene(_playSession.getAuthoringScene());
        _layer->setSceneContext(scene);
        _layer->selectEntity(scene && selectedUUID != 0 ? scene->getEntityByUUID(selectedUUID) : nullptr);
    }

    void onSceneDestroyed(App& app, Scene* scene) override
    {
        (void)app;
        _playSession.onSceneDestroyed(scene);
        if (_layer) {
            _layer->setEditableScene(_playSession.getAuthoringScene());
            _layer->selectEntity(nullptr);
        }
    }

    void onNativeEvent(App& app, const SDL_Event& event) override
    {
        (void)app;
        ImGuiManager::get().processEvents(const_cast<SDL_Event&>(event));
    }

    bool onEvent(App& app, const Event& event) override
    {
        (void)app;
        if (ImGuiManager::get().processEvent(event) != EventProcessState::Continue) {
            return true;
        }
        if (_layer) {
            _layer->onEvent(event);
        }
        return false;
    }

    void onLogic(App& app, float dt) override
    {
        if (!_layer) {
            return;
        }

        if (auto* renderRuntime = app.getRenderRuntime()) {
            auto& editorCamera = _layer->getCamera();
            const Extent2D viewportExtent = renderRuntime->getViewportExtent();
            if (_layer->shouldCaptureInput()) {
                _cameraController.update(editorCamera, app.inputManager, dt);
            }
            if (viewportExtent.height > 0) {
                editorCamera.setPerspective(editorCamera._fov,
                                            static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height),
                                            editorCamera._nearClip,
                                            editorCamera._farClip);
            }
            app.setExtensionRenderFrameState({
                .view       = editorCamera.getViewMatrix(),
                .projection = editorCamera.getProjectionMatrix(),
                .cameraPos  = editorCamera.getPosition(),
            });
        }

        _layer->onUpdate(dt);
        Rect2D pendingRect;
        if (_layer->getPendingViewportResize(pendingRect)) {
            if (auto* renderRuntime = app.getRenderRuntime()) {
                renderRuntime->onViewportResized(pendingRect);
            }
        }
    }

    void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)dt;
        if (!_layer) {
            return;
        }

        if (auto* renderRuntime = app.getRenderRuntime()) {
            _layer->setViewportContext(renderRuntime->buildViewportSnapshot());
        }

        ImGuiManager::get().beginFrame();
        _layer->onImGuiRender();
        renderRuntimeToolsWindow(app, *_layer, _cameraController, dt);
        ImGuiManager::get().endFrame();
        ImGuiManager::get().render();

        if (auto* render = app.getRender(); render && render->getAPI() == ERenderAPI::Vulkan) {
            ImGuiManager::get().submitVulkan(commandBuffer.getHandleAs<VkCommandBuffer>());
        }
    }

  private:
    std::unique_ptr<EditorLayer> _layer;
    EditorPlaySession             _playSession;
    FreeCameraController          _cameraController;
};

} // namespace

std::unique_ptr<IAppExtension> createEditorAppExtension()
{
    return std::make_unique<EditorAppExtension>();
}

EditorLayer* getEditorLayer()
{
    return gEditorLayer;
}

} // namespace ya
