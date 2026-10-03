#include "GameEditor/EditorModule.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "Core/Os/OsEvent.h"
#include "Core/Profiling/Profiling.h"
#include "Core/Scripting/ScriptApiRegistry.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "Scene2D/Sprite2DComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/CameraController/FreeCameraController.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "ECS/Systems/Components/TerrainComponent.h"
#include "GameEditor/EditorChrome.h"
#include "GameEditor/EditorViewProducer.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorPlaySession.h"
#include "GameEditor/EditorProfilingSettings.h"
#include "GameEditor/EditorRuntimeSettings.h"
#include "GameEditor/Input/EditorInputNode.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Dock/EditorNativeTearOff.h"
#include "GameEditor/UI/Shell/EditorSurfaceContext.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"
#include "GameEditor/UI/Viewport/EditorUICanvasCompositor.h"
#include "GameEditor/UI/Viewport/EditorViewportCompositor.h"
#include "GameEditor/UI/Dock/EditorWindowLayout.h"
#include "GameEditor/UI/Dock/EditorLayoutLibrary.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"
#include "GameEditor/UI/Shell/EditorLaunchFlow.h"
#include "GameRuntime/App.h"
#include "Render3D/Common/SceneViewDesc.h"
#include "GameRuntime/Automation/EditorAutomationControl.h"
#include "GameRuntime/IRuntimeModule.h"
#include "GUI/Compose/GuiFrameInspectorOverlay.h"
#include "GUI/Compose/Render2DComposePass.h"
#include "GUI/Host/GUIAppDelegate.h"
#include "GUI/Host/GUIAppHost.h"
#include "GUI/Host/GUIDragRouter.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/GUIWindowManager.h"
#include "RHI/Core/PresentFrame.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/CommandBuffer.h"
#include "Render3D/RenderDeviceState.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Core/Texture.h"
#include "RHI/NativeWindow.h"
#include "RHI/Render.h"
#include "Render/Resources/FontManager.h"
#include "Render3D/Common/RenderViewOutput.h"
#include "Render3D/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/Node3D.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ya
{

namespace
{

// =============================================================================
// Editor GUI frame chain — read the four IRuntimeModule hooks in this file.
//
// AppKernel
//   GameRuntimeTickOrchestrator::iterate
//     tickLogic
//       EditorModule::onLogic
//         setSceneContext (play clone while running)
//         updateEditorCameraAndPrepareCompose   (world graph on/off, camera,
//                                                Render2D pipeline prep)
//         EditorLayer::onUpdate
//         (the authoring View's rect is declared by EditorViewProducer, not
//          pushed at the renderer from here)
//     tickRender
//       Renderer record world graph
//       EditorModule::onViewportCompose          [command recording]
//         viewport snapshot → EditorViewportCompositor
//           overlays on the tone-mapped display image (world, then screen)
//         setViewportDisplayImage  (chrome UIImage samples this RT)
//         EditorUICanvasCompositor (only while the UI Designer Canvas tab is
//           attached): preview tree + grid + selection → designer.canvas().image
//       EditorModule::onPresentation             [same command buffer]
//         presentDefaultChrome
//           EditorWindowSession::tick → EditorSurface::tick
//             rebuild-if-needed → metrics → WidgetTree::tick
//             shell dialogs → pushViewportDisplay → buildSnapshot
//             publishViewportRect → viewport overlay host
//           replayUIFrameSnapshot(EditorToolSurface) + frame inspector
//     EditorModule::recordExtraSurfaces          [before the frame submit]
//         sweepAndRecordExtraWindows
//           close requested extras | reclaim empty | orphan GUI sessions
//           GUIWindowManager::tickTrees + recordAll
//     one submitFrame (host + every acquired extra) then present each
//
// Input is not in this render chain: EditorInputNode → session.dispatchEvent
//   → WidgetTree. Viewport gizmo overlay is Exclusive only during LMB drag.
// Overlay *drawing* is recorded inside the compositor pass, not chrome tick.
// =============================================================================

EditorLayer* gEditorLayer          = nullptr;
Scene*       gEditorAuthoringScene = nullptr;

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
    auto&       editorCamera = layer.getCamera();
    const auto& desc         = app.getDesc();
    const float aspect       = desc.height > 0 ? static_cast<float>(desc.width) / static_cast<float>(desc.height) : (16.0f / 9.0f);
    editorCamera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
    editorCamera.setPositionAndRotation(resolveInitialEditorCameraPosition(app),
                                        resolveInitialEditorCameraRotation(app));
}

class EditorModule final : public IModule, public IRuntimeModule, public IEditorAutomationControl
{
  private:
    std::unique_ptr<EditorLayer>   _layer;
    /// Owns the editor's view declarations (authoring view, camera preview).
    EditorViewProducer             _viewProducer;
    EditorPlaySession              _playSession;
    FreeCameraController           _cameraController;
    EditorViewportCompositor       _viewportCompositor;
    EditorUICanvasCompositor       _canvasCompositor;
    ScreenDrawRecorder             _toolRecorder;
    EditorDocumentRegistry         _documents;
    EditorWindowRegistry           _windows;
    EditorTabSpawnerRegistry       _tabSpawners;
    GUIWindowManager               _guiWindows;
    GUIDragRouter                  _dragRouter;
    EditorLaunchFlow               _launchFlow;
    struct FExtraGuiDelegate final : IGUIAppDelegate
    {
        void buildUI(WidgetTree&) override {}
    } _extraGuiContent;
    EditorInputNode                _inputNode;
    InputRouter::FNodeRegistration _inputNodeRegistration;
    DelegateHandle                 _scenePathHandle = INVALID_HANDLE;
    App*                           _app             = nullptr;
    EEditorChromeHost              _chromeHost      = EEditorChromeHost::WidgetTree;

    [[nodiscard]] INativeWindow* mainNativeWindow() const
    {
        if (!_app) {
            return nullptr;
        }
        // The editor's shell window: the window this app presents. Its own
        // window becomes a per-window identity when a frame can present more
        // than one (the host's session registry takes over the primary window
        // too), so ask the app rather than naming the renderer's primary.
        if (IRenderSurfaceContext* surface = _app->getRenderServices().getHostSurface()) {
            return surface->getNativeWindow();
        }
        return nullptr;
    }

    [[nodiscard]] FEditorNativeTearOff tearOffEnv()
    {
        return {
            .coordinator = &_guiWindows,
            .content     = &_extraGuiContent,
            .windows     = &_windows,
            .spawners    = &_tabSpawners,
            .documents   = &_documents,
            .render      = _app ? _app->getRenderServices().getRender() : nullptr,
        };
    }

    void persistLayout()
    {
        persistEditorWindowLayout(_windows, mainNativeWindow(), &_guiWindows);
    }

    void hookExtraPersist(EditorWindowSession& session)
    {
        session.surface().setPersistLayout([this]() { persistLayout(); });
        const auto persist = [this]() { persistLayout(); };
        if (FDockContext* dock = session.surface().windowRootDock()) {
            dock->appendOnDockUpdated(persist);
            dock->appendOnFloatingUpdated(persist);
        }
        if (FDockContext* dock = session.surface().ownedNestedDock()) {
            dock->appendOnDockUpdated(persist);
            dock->appendOnFloatingUpdated(persist);
        }
    }

    void hookNativeTearOff(EditorWindowSession& session)
    {
        const EditorWindowId windowId = session.windowId();
        session.surface().setOnDockNoTargetTearOff(
            [this, windowId](FDockContext& dock, uint64_t panelId, const glm::vec2& pos, const glm::vec2& size) {
                EditorWindowSession* source = _windows.find(windowId);
                if (!source) {
                    return false;
                }
                INativeWindow* native = nullptr;
                if (windowId == kDefaultEditorWindowId) {
                    native = mainNativeWindow();
                }
                else if (source->hostGuiWindowId() != 0) {
                    native = _guiWindows.findNative(source->hostGuiWindowId());
                }
                FEditorNativeTearOff env = tearOffEnv();
                FEditorTearOffResult torn;
                const bool handled =
                    handleDockNoTargetTearOff(env, *source, dock, panelId, pos, size, native, &torn);
                if (torn.editorWindowId != kInvalidEditorWindowId) {
                    if (EditorWindowSession* extra = _windows.find(torn.editorWindowId)) {
                        hookExtraPersist(*extra);
                        hookNativeTearOff(*extra);
                    }
                }
                return handled;
            });
    }

    void registerEditorPresets()
    {
        auto& registry = editor::NodeCreateRegistry::get();

        registry.registerPreset(
            "2D Object",
            "Sprite",
            "Scene sprite on the entity XY plane. Texture, size, and tint are authored data.",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        node3D->getEntity()->addComponent<Sprite2DComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "3D Object",
            "Cube",
            "Static cube with a Phong material",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        Entity* entity = node3D->getEntity();
                        auto*   mc     = entity->addComponent<StaticMeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
                        entity->addComponent<PhongMaterialComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "3D Object",
            "Sphere",
            "Static sphere with a Phong material",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        Entity* entity = node3D->getEntity();
                        auto*   mc     = entity->addComponent<StaticMeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Sphere);
                        entity->addComponent<PhongMaterialComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "3D Object",
            "Plane",
            "Static quad with a Phong material",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        Entity* entity = node3D->getEntity();
                        auto*   mc     = entity->addComponent<StaticMeshComponent>();
                        mc->setPrimitiveGeometry(EPrimitiveGeometry::Quad);
                        entity->addComponent<PhongMaterialComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "3D Object",
            "Terrain",
            "Height-map terrain with a Phong material",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        Entity* entity = node3D->getEntity();
                        entity->addComponent<TerrainComponent>();
                        entity->addComponent<PhongMaterialComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "Light",
            "Point Light",
            "Omnidirectional point light",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        node3D->getEntity()->addComponent<PointLightComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "Light",
            "Directional Light",
            "Sun-style directional light",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        node3D->getEntity()->addComponent<DirectionalLightComponent>();
                    }
                }
                return node;
            });

        registry.registerPreset(
            "Camera",
            "Camera",
            "World camera with a default visualization mesh",
            [](Scene& scene, const std::string& name, Node* parent) -> Node* {
                Node* node = scene.createNode3D(name, parent);
                if (node) {
                    if (auto* node3D = dynamic_cast<Node3D*>(node)) {
                        node3D->getEntity()->addComponent<CameraComponent>();
                    }
                }
                return node;
            });
    }

    void registerEditorScriptApis()
    {
        using Json = ScriptApiRegistry::Json;
        auto& api  = ScriptApiRegistry::get();

        api.registerFunction(
            "project.open",
            "Opens a project through the launch flow (browser window must be up). "
            "Args: {path}.",
            Json{{"path", {{"type", "string"}}}},
            [this](const Json& args) -> Json {
                const std::string path = args.value("path", "");
                if (path.empty()) {
                    throw ScriptApiRegistry::Error("project.open: path is required");
                }
                if (!_launchFlow.requestOpenProject(path)) {
                    throw ScriptApiRegistry::Error("project.open: no project browser session to open from");
                }
                return Json{{"requested", true}};
            });

        api.registerFunction(
            "ui_designer.open",
            "Opens a .yaui.json asset in the UI Designer (same funnel as the "
            "content browser / inspector / hierarchy entries). Args: {path}.",
            Json{{"path", {{"type", "string"}}}},
            [this](const Json& args) -> Json {
                const std::string path = args.value("path", "");
                if (path.empty()) {
                    throw ScriptApiRegistry::Error("ui_designer.open: path is required");
                }
                if (!_layer || !_layer->openDocumentEditor(EEditorDocumentKind::UI, path)) {
                    throw ScriptApiRegistry::Error("ui_designer.open: failed to open '" + path + "'");
                }
                return Json{{"opened", path}};
            });

        api.registerFunction(
            "runtime.play",
            "Enters Play (PIE) mode on the next tick. Args: none.",
            Json::object(),
            [this](const Json&) -> Json {
                App* app = App::get();
                if (!app) {
                    throw ScriptApiRegistry::Error("runtime.play: no app");
                }
                app->getTaskManager().registerTickTask([app]() { app->startRuntime(); });
                return Json{{"requested", true}};
            });

        api.registerFunction(
            "runtime.stop",
            "Leaves Play / Simulate mode on the next tick. Args: none.",
            Json::object(),
            [this](const Json&) -> Json {
                App* app = App::get();
                if (!app) {
                    throw ScriptApiRegistry::Error("runtime.stop: no app");
                }
                app->getTaskManager().registerTickTask([app]() {
                    if (app->isRuntimeMode()) {
                        app->stopRuntime();
                    }
                    else if (app->isSimulationMode()) {
                        app->stopSimulation();
                    }
                });
                return Json{{"requested", true}};
            });

        api.registerFunction(
            "ui_designer.pan_zoom",
            "Sets the UI Designer Canvas navigation. Args: {pan_x?, pan_y?, zoom?}.",
            Json{{"pan_x", {{"type", "number"}}}, {"pan_y", {{"type", "number"}}}, {"zoom", {{"type", "number"}}}},
            [this](const Json& args) -> Json {
                EditorUICanvasView& canvas = _layer->getEditorUIDesignerSession().canvas();
                canvas.pan.x = args.value("pan_x", canvas.pan.x);
                canvas.pan.y = args.value("pan_y", canvas.pan.y);
                if (args.contains("zoom")) {
                    canvas.setZoom(args.at("zoom").get<float>());
                }
                return Json{{"pan_x", canvas.pan.x}, {"pan_y", canvas.pan.y}, {"zoom", canvas.zoom}};
            });

        api.registerFunction(
            "scene.create_preset",
            "Creates a node from the editor create registry. Args: {preset, name?, parent_path?}. "
            "Presets: Sprite, Cube, Sphere, Plane, Terrain, Point Light, Directional Light, Camera.",
            Json{{"preset", {{"type", "string"}}},
                 {"name", {{"type", "string"}}},
                 {"parent_path", {{"type", "string"}}}},
            [this](const Json& args) -> Json {
                Scene* scene = ScriptApiRegistry::get().getActiveScene();
                if (!scene) {
                    throw ScriptApiRegistry::Error("no active scene");
                }
                const std::string preset = args.at("preset").get<std::string>();
                const std::string name   = args.value("name", preset);

                Node* parent = nullptr;
                if (const auto it = args.find("parent_path"); it != args.end() && !it->is_null()) {
                    parent = scene->findNodeByPath(it->get<std::string>());
                    if (!parent) {
                        throw ScriptApiRegistry::Error(std::format("parent_path not found: {}", it->get<std::string>()));
                    }
                }

                Node* node = nullptr;
                node = editor::NodeCreateRegistry::get().createPreset(preset, *scene, name, parent);
                if (!node) {
                    throw ScriptApiRegistry::Error(std::format("unknown create preset: {}", preset));
                }
                return Json{{"path", scene->getNodePath(node)}, {"name", node->getName()}};
            });
    }

    // -----------------------------------------------------------------
    // Per-frame steps. The four IRuntimeModule hooks below are the
    // table of contents; these helpers are the named steps in that map.
    // -----------------------------------------------------------------

    /// While the game owns the world viewport (PIE), its display root renders
    /// where the editor shows it: the panel rect, not the window. The Hold
    /// policy keeps window resizes out (the editor panel is the viewport, not
    /// the window); this seed is the panel's own per-tick write, so a dock
    /// resize or layout change re-fits the game image instead of stretching a
    /// stale startup resolution into the new rect.
    void seedRuntimeResolutionFromViewport(App& app, AppRenderServices& renderServices)
    {
        if (!app.isRuntimeMode() || !_layer->isViewportShown()) {
            return;
        }
        const Rect2D viewportRect = _layer->getViewportRect();
        if (viewportRect.extent.x > 0.0f && viewportRect.extent.y > 0.0f) {
            const glm::vec2 device = devicePixelExtent(viewportRect.extent, _layer->viewportPixelDensity());
            renderServices.seedRenderResolution(Extent2D{
                .width  = static_cast<uint32_t>(device.x),
                .height = static_cast<uint32_t>(device.y),
            });
        }
    }

    void updateEditorCameraAndPrepareCompose(App& app, float dt)
    {
        auto& renderServices = app.getRenderServices();
        if (!renderServices.hasRenderer()) {
            return;
        }

        // The panel stays in logical points. The view target and the game UI
        // density are the window's device pixels per point, published by the
        // surface with the panel rect.
        renderServices.setPixelDensity(_layer->viewportPixelDensity());
        renderServices.setLogicalViewport(_layer->getViewportRect());
        seedRuntimeResolutionFromViewport(app, renderServices);

        auto& editorCamera = _layer->getCamera();
        // Aspect follows the extent the displayed View actually rendered at --
        // read from that View's output, since the app names which View it
        // displays. Nothing here derives it from the window.
        const RenderViewOutput* displayedView = renderServices.getDisplayedViewOutput();
        const Extent2D viewExtent = displayedView ? displayedView->desc.extent : Extent2D{};
        // Keep the editor camera controllable during simulation; only full
        // runtime (PIE) hands viewport input over to the game.
        if (!app.isRuntimeMode() && _layer->shouldCaptureInput()) {
            _cameraController.update(editorCamera, app.getInputManager(), dt);
        }
        if (viewExtent.height > 0) {
            const float aspect = static_cast<float>(viewExtent.width) / static_cast<float>(viewExtent.height);
            if (_layer->isEditorOrthoXY()) {
                editorCamera.setRotation({0.0f, 0.0f, 0.0f});
                constexpr float kHalfHeight = 12.0f;
                editorCamera.setOrthographic(-kHalfHeight * aspect,
                                             kHalfHeight * aspect,
                                             -kHalfHeight,
                                             kHalfHeight,
                                             editorCamera._nearClip,
                                             editorCamera._farClip);
            }
            else {
                editorCamera.setPerspective(editorCamera._fov,
                                            aspect,
                                            editorCamera._nearClip,
                                            editorCamera._farClip);
            }
        }
        // Prepare before command recording. Recreating a pipeline while a
        // command buffer is recording invalidates that command buffer.
        // 3D overlays draw into the tone-mapped display image and test the
        // View depth. The UI Designer canvas is a separate target.
        const EFormat::T depthFormat = renderServices.getViewDepthFormat();
        EFormat::T overlayColor = EFormat::R8G8B8A8_UNORM;
        if (RenderDeviceState* device = renderServices.getDeviceState()) {
            overlayColor = device->getPostprocessColorFormat();
        }
        _viewportCompositor.prepare(overlayColor, depthFormat);
        _canvasCompositor.prepare();
        EFormat::T chromeFormat = EFormat::B8G8R8A8_UNORM;
        if (auto* surface = renderServices.getHostSurface(); surface && surface->getSwapchain()) {
            chromeFormat = surface->getSwapchain()->getFormat();
        }
        _toolRecorder.prepare(chromeFormat, EFormat::Undefined);
    }

    void composeAuthoringViewport(App& app, ICommandBuffer& commandBuffer)
    {
        auto& renderServices = app.getRenderServices();
        auto* render         = renderServices.getRender();
        if (!renderServices.hasRenderer() || !render) {
            _layer->setViewportDisplayImage(nullptr);
            return;
        }

        // No viewport on screen: nothing to compose into. The authoring View was
        // not declared this tick either (see EditorViewProducer), so there is no
        // image to compose and no widget to show it; recording the pass anyway
        // would draw the fallback empty image into a target nothing samples.
        // Both images are cleared rather than left standing so a later re-show
        // cannot pick up a handle from the last time the tab was visible: the
        // tick that re-shows the viewport composes before chrome pushes the
        // display, so it gets this tick's image rather than a stale one.
        if (!_layer->isViewportShown()) {
            _layer->setViewportDisplayImage(nullptr);
            _layer->setViewportPreviewImage(nullptr);
            return;
        }

        const auto snapshot = renderServices.buildViewportSnapshot(app.getSceneServices().getActiveScene());
        _layer->setViewportContext(snapshot);
        _layer->setEntityIdPickImage(snapshot.entityIdImageOwner);
        // The compositor wants a camera, not "the displayed View": this caller
        // answers with the displayed View's camera today; another View's camera
        // composes just as well.
        const DisplayedView& displayedArrangement = renderServices.getDisplayedView();
        const EditorComposeCamera worldCamera{
            .position       = displayedArrangement.cameraPos,
            .view           = displayedArrangement.view,
            .projection     = displayedArrangement.projection,
            .viewProjection = displayedArrangement.viewProjection(),
        };
        _viewportCompositor.compose(commandBuffer, snapshot, *_layer, worldCamera);
        // Keep the last valid frame instead of clobbering the display with a
        // transiently null output (startup / mode-switch / resize gaps).
        if (auto output = _viewportCompositor.getOutputImage();
            output && output->isValid() && output->getImageView()) {
            _layer->setViewportDisplayImage(std::move(output));
        }

        // The camera preview is the editor's own View, and it is shown as
        // viewport chrome: read its image here (the world graph has already
        // recorded it, this flight is still open) and hand it to the layer, the
        // same three steps the world viewport uses: device -> layer -> chrome
        // widget. The chrome composes it after the world image, which is what
        // keeps the world overlays under it. No preview this tick (no camera
        // selected) publishes null, which collapses the panel.
        _layer->setViewportPreviewImage(
            previewImageForChrome(app, commandBuffer));
    }

    /// The preview View's image as chrome can sample it: wrapped for the GUI and
    /// moved to a readable layout. Null when the tick recorded no preview View.
    std::shared_ptr<Texture> previewImageForChrome(App& app, ICommandBuffer& commandBuffer)
    {
        auto& renderServices = app.getRenderServices();
        if (!renderServices.hasRenderer()) {
            return nullptr;
        }
        // The preview's identity belongs to this producer, not to a global slot:
        // ask the producer that declares it which View to read.
        const RenderViewOutput* output =
            renderServices.getViewOutput(_viewProducer.previewKey().viewId());
        if (!output) {
            return nullptr;
        }
        auto display = output->displayImage();
        if (!display || !display->getImageShared() || !display->getImageViewShared()) {
            return nullptr;
        }
        commandBuffer.transitionImageLayoutAuto(display->getImage(), EImageLayout::ShaderReadOnlyOptimal);
        return Texture::wrap(display->getImageShared(),
                             display->getImageViewShared(),
                             "EditorCameraPreview");
    }

    void presentDefaultChrome(App& app, ICommandBuffer& commandBuffer, float dt)
    {
        // Launch phases first: the pending open may block here while the
        // splash overlay's last presented frame is on screen, and the editor
        // window shows only after the chrome below has been built.
        _launchFlow.tick();
        // Runtime-with-control is the game's viewport: gizmos and their W/E/R
        // switch belong to authoring sessions only.
        if (_layer) {
            _layer->gizmo().setRuntimeActive(app.isRuntimeMode());
        }
        auto* render = app.getRenderServices().getRender();
        if (!render) {
            return;
        }
        EditorWindowSession* session = _windows.find(kDefaultEditorWindowId);
        if (!session) {
            return;
        }
        IRenderSurfaceContext* surface = app.getRenderServices().getHostSurface();
        if (!surface) {
            return;
        }
        const FEditorSurfaceContext surfaceContext = makeEditorSurfaceContext(
            app,
            *surface,
            app.getRenderServices().getDisplayedView());
        session->tick(surfaceContext, dt);
        const UIFrameSnapshot& snapshot = session->snapshot();
        const Extent2D targetExtent = surface->getSwapchain()
                                          ? surface->getSwapchain()->getExtent()
                                          : Extent2D{};
        EFormat::T chromeFormat = EFormat::B8G8R8A8_UNORM;
        if (surface->getSwapchain()) {
            chromeFormat = surface->getSwapchain()->getFormat();
        }
        replayUIFrameSnapshot(&commandBuffer,
                              snapshot,
                              targetExtent,
                              chromeFormat,
                              _toolRecorder,
                              [&session, &snapshot, &targetExtent](ScreenDrawList& composeList) {
                                  if (WidgetTree* tree = session->tree()) {
                                      runGuiFrameInspectorOverlay(*tree, snapshot, composeList, targetExtent);
                                  }
                              });
    }

    void sweepAndRecordExtraWindows(float dt, FFrameSubmission& submission)
    {
        FEditorNativeTearOff env = tearOffEnv();
        std::vector<EditorWindowId> closing;
        _windows.forEach([&](EditorWindowSession& session) {
            if (session.windowId() == kDefaultEditorWindowId || session.hostGuiWindowId() == 0) {
                return;
            }
            if (IGUIWindowSession* gui = _guiWindows.findSession(session.hostGuiWindowId())) {
                if (gui->closeRequested()) {
                    closing.push_back(session.windowId());
                }
            }
        });
        for (EditorWindowId id : closing) {
            (void)closeEditorWindow(env, id);
        }

        std::vector<EditorWindowId> extras;
        _windows.forEach([&](EditorWindowSession& session) {
            if (session.windowId() != kDefaultEditorWindowId) {
                extras.push_back(session.windowId());
            }
        });
        for (EditorWindowId id : extras) {
            (void)reclaimEditorWindowIfEmpty(env, id);
        }

        std::vector<GUIWindowId> hosted;
        _windows.forEach([&](EditorWindowSession& session) {
            if (session.hostGuiWindowId() != 0) {
                hosted.push_back(session.hostGuiWindowId());
            }
        });
        std::vector<GUIWindowId> orphans;
        _guiWindows.forEachWindow([&](GUIWindowId id, WidgetTree*, INativeWindow*) {
            if (_guiWindows.isHostOverlay(id)) {
                return;
            }
            if (_launchFlow.hostsSession(id)) {
                return;
            }
            if (std::find(hosted.begin(), hosted.end(), id) == hosted.end()) {
                orphans.push_back(id);
            }
        });
        for (GUIWindowId id : orphans) {
            (void)_guiWindows.destroySession(id);
        }

        if (_guiWindows.extraWindowCount() == 0) {
            return;
        }
        _guiWindows.tickTrees(dt);
        _guiWindows.recordAll(submission);
    }

  public:
    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override { persistLayout(); }
    void onUnload() override {}

    /// The editor's chrome fills every window it hosts: each shell root paints
    /// an opaque fill over the whole of its own surface and shows the world
    /// inside a viewport widget (a torn-off window is a second shell root on a
    /// second surface, not a different kind of window). That holds in every
    /// state the module is loaded in, including a play session -- Play runs the
    /// game in Runtime mode but the window is still the editor's. A copy of the
    /// View across the surface before that chrome records is therefore a
    /// full-surface draw nothing can see.
    [[nodiscard]] bool fillsSurface(const IRenderSurfaceContext&) const override { return _layer != nullptr; }

    void onConfigure(App& app, AppDesc& desc) override
    {
        ConfigManager::get().openDocument("editor", "Engine/Saved/Config/Editor.json");
        editor_runtime_settings::migrateLegacy();
        if (!shadow_settings::hasRuntimeSettings()) {
            shadow_settings::saveRuntimeSettings(
                shadow_settings::loadSettingsFromDocument("editor", app.getRenderServices().getShadowSettings()));
        }
        editor_profiling_settings::load();
        editor_runtime_settings::load();
        if (desc.projectPath && !desc.defaultScenePath) {
            const std::string path = ConfigManager::get().getOr<std::string>("editor", "startup.defaultScenePath", "");
            if (!path.empty()) {
                desc.defaultScenePath = path;
            }
        }

        _chromeHost = EEditorChromeHost::WidgetTree;
        const std::string fromConfig = ConfigManager::get().getOr<std::string>("editor", "chrome.host", "widgettree");
        EEditorChromeHost requested = EEditorChromeHost::WidgetTree;
        if (!tryParseEditorChromeHost(fromConfig, requested)) {
            YA_CORE_WARN("Ignoring invalid editor.chrome.host '{}', using widgettree", fromConfig);
        }
        else if (requested == EEditorChromeHost::ImGui) {
            YA_CORE_WARN("Ignoring editor.chrome.host=imgui; WidgetTree is the only editor chrome host");
        }
        if (desc.editorChrome) {
            if (!tryParseEditorChromeHost(*desc.editorChrome, requested)) {
                YA_CORE_WARN("Ignoring invalid --editor-chrome '{}'", *desc.editorChrome);
            }
            else if (requested == EEditorChromeHost::ImGui) {
                YA_CORE_WARN("Ignoring --editor-chrome=imgui; WidgetTree is the only editor chrome host");
            }
        }
        _chromeHost = EEditorChromeHost::WidgetTree;
    }

    void onAttach(App& app) override
    {
        auto& renderServices = app.getRenderServices();
        YA_CORE_ASSERT(renderServices.hasRenderer(),
                       "Editor extension requires an initialized renderer");
        // The game view inside the editor is a panel, not the window. Leave
        // the seeded resolution where init put it.
        renderServices.holdRenderResolution();

        // Layout roots are the product's decision, not the framework's: the
        // editor here, and a standalone GUI app its own pair. Command line
        // wins over the editor config, so a run can point at a scratch tree
        // (tests, a packaged build) without editing anything on disk.
        {
            FEditorLayoutRoots roots = EditorLayoutLibrary::get().roots();
            const AppDesc& desc = app.getDesc();
            if (desc.layoutDefaultsRoot && !desc.layoutDefaultsRoot->empty()) {
                roots.defaults = *desc.layoutDefaultsRoot;
            }
            else {
                roots.defaults = ConfigManager::get().getOr<std::string>(
                    "editor", "layout.defaultsRoot", roots.defaults);
            }
            if (desc.layoutOverridesRoot && !desc.layoutOverridesRoot->empty()) {
                roots.overrides = *desc.layoutOverridesRoot;
            }
            else {
                roots.overrides = ConfigManager::get().getOr<std::string>(
                    "editor", "layout.overridesRoot", roots.overrides);
            }
            EditorLayoutLibrary::get().bindRoots(std::move(roots));
        }

        _layer = std::make_unique<EditorLayer>(&app);
        initializeEditorCamera(app, *_layer);
        _layer->setCurrentScenePath(app.getDesc().defaultScenePath.value_or(std::string{}));
        _layer->onAttach();
        registerBuiltinEditorTabSpawners(_tabSpawners);
        _layer->bindDocumentServices(&_documents, app.getUIDocumentStore());
        EditorWindowSession* window = _windows.find(kDefaultEditorWindowId);
        YA_CORE_ASSERT(window, "EditorWindowRegistry always owns the default editor window");
        window->bind(*_layer, &_tabSpawners, &_documents);
        _app = &app;
        if (RenderDeviceState* device = renderServices.getDeviceState()) {
            _guiWindows.setScreenDrawPipelines(&device->screenDrawPipelines());
            _viewportCompositor.bindDraw(device->screenDrawPipelines(), device->worldDrawPipelines());
            _canvasCompositor.bindDraw(device->screenDrawPipelines());
            _toolRecorder.init(device->screenDrawPipelines());
        }
        if (!_guiWindows.init()) {
            YA_CORE_WARN("EditorModule: extra native window coordinator failed to init");
        }
        _dragRouter.bindExtras(&_guiWindows);
        _dragRouter.bindRender(app.getRenderServices().getRender());
        INativeWindow* mainNative = mainNativeWindow();
        _launchFlow.begin(_guiWindows, app.getRenderServices().getRender(), *_layer, mainNative);
        window->surface().setPersistLayout([this]() { persistLayout(); });
        hookNativeTearOff(*window);
        // The arrangement is a document of its own under the layout overrides
        // root, not a key in Editor.json (see EditorLayoutLibrary).
        const nlohmann::json savedLayout =
            EditorLayoutLibrary::get().document(kEditorLayoutWorkspace);
        if (mainNative && savedLayout.is_object() && !savedLayout.empty()) {
            if (const nlohmann::json* main = findMainEditorWindowRecord(savedLayout)) {
                (void)recoverEditorWindowPlacement(*mainNative, *main);
            }
            FEditorNativeTearOff env = tearOffEnv();
            const size_t restored = restoreEditorExtraWindows(env, savedLayout);
            if (restored > 0) {
                YA_CORE_INFO("EditorModule: restored {} extra native window(s)", restored);
            }
            _windows.forEach([this](EditorWindowSession& session) {
                if (session.windowId() != kDefaultEditorWindowId) {
                    hookExtraPersist(session);
                    hookNativeTearOff(session);
                }
            });
        }
        _scenePathHandle = _layer->onScenePathChanged.addLambda(this, [this]() {
            const std::string& path = _layer->getCurrentScenePath();
            _windows.forEach([this, &path](EditorWindowSession& session) {
                session.bindSceneDocument(_documents, path);
            });
        });
        _layer->setSaveSceneAsHandler([this]() {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                session->surface().openSceneSaveDialog();
            }
        });
        _layer->setAssetPickerHandler([this](type_index_t refType,
                                             std::string currentPath,
                                             std::function<void(std::string)> onPicked) {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                session->surface().openAssetPickerDialog(refType, std::move(currentPath), std::move(onPicked));
            }
        });
        _layer->setShowContentBrowserHandler([this]() {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                session->surface().showContentBrowser();
            }
        });
        _layer->setShowUIDesignerCanvasHandler([this]() {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                (void)session->surface().invokeTab("ui-preview");
            }
        });
        _layer->setOpenDocumentEditorHandler([this](EEditorDocumentKind kind, std::string key) -> bool {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                return session->surface().openDocumentEditor(kind, std::move(key));
            }
            return false;
        });
        _layer->setFilePickerHandler([this](FEditorFilePickerRequest request) {
            if (EditorWindowSession* session = _windows.find(kDefaultEditorWindowId)) {
                session->surface().openFilePickerDialog(std::move(request));
            }
        });
        _inputNode.bind(app, *_layer, _windows, window->windowId(), &_guiWindows, &_dragRouter);
        _inputNodeRegistration = app.getInputRouter().registerNode(_inputNode);
        gEditorLayer           = _layer.get();
        // The editor owns its viewport, so it declares the views in it: the
        // authoring view while that is what the user sees, and the camera
        // preview inset of the camera the user selected.
        _viewProducer.bind(app, *_layer);
        app.addSceneViewProducer(_viewProducer);
        registerEditorPresets();
        registerEditorScriptApis();
        YA_CORE_INFO("Editor chrome host: {}", editorChromeHostName(_chromeHost));
    }

    void* queryInterface(FInterfaceId interfaceId) override
    {
        if (interfaceId == YA_RUNTIME_MODULE_INTERFACE) {
            return static_cast<IRuntimeModule*>(this);
        }
        if (interfaceId == YA_EDITOR_AUTOMATION_CONTROL_INTERFACE) {
            return static_cast<IEditorAutomationControl*>(this);
        }
        return nullptr;
    }

    [[nodiscard]] Scene* getAuthoringScene() const override
    {
        return _playSession.getAuthoringScene();
    }

    bool setEditorCameraTransform(const glm::vec3& position, const glm::vec3& rotation) override
    {
        if (!_layer) {
            return false;
        }
        _layer->getCamera().setPositionAndRotation(position, rotation);
        return true;
    }

    bool focusEditorCameraOnWorldPoint(const glm::vec3& target,
                                       float            distance,
                                       float            heightOffset) override
    {
        if (!_layer) {
            return false;
        }

        const float safeDistance = std::max(distance, 0.2f);
        glm::vec3   offset       = glm::normalize(glm::vec3(1.0f, 0.35f, 1.0f));
        glm::vec3   position     = target + offset * safeDistance + glm::vec3(0.0f, heightOffset, 0.0f);

        glm::vec3 toTarget  = glm::normalize(target - position);
        float     pitch     = glm::degrees(std::asin(glm::clamp(toTarget.y, -1.0f, 1.0f)));
        glm::vec3 yawVector = toTarget;
        if constexpr (FMath::Vector::IsRightHanded) {
            yawVector.z = -yawVector.z;
            yawVector.x = -yawVector.x;
        }
        float yaw = glm::degrees(std::atan2(yawVector.x, yawVector.z));

        _layer->getCamera().setPositionAndRotation(position, glm::vec3(pitch, yaw, 0.0f));
        return true;
    }

    bool setEditorGizmosVisible(bool bVisible) override
    {
        if (!_layer) {
            return false;
        }
        _layer->setEditorGizmoShown(bVisible);
        return true;
    }

    void onDetach(App& app) override
    {
        _launchFlow.shutdown();
        app.getInputRouter().cancelInput(EInputCancelReason::ModuleDetached);
        _inputNodeRegistration.reset();
        _inputNode.unbind();
        _dragRouter.unbind();
        // The docks only exist until the sessions are shut down below, and this
        // is the last point that still has them. `onStop` cannot be relied on
        // for this: it runs from ModuleManager's teardown, by which time the
        // arrangement has already been destroyed.
        persistLayout();
        if (_layer && _scenePathHandle != INVALID_HANDLE) {
            _layer->onScenePathChanged.remove(_scenePathHandle);
            _scenePathHandle = INVALID_HANDLE;
        }
        _windows.forEach([](EditorWindowSession& session) {
            session.surface().setPersistLayout(nullptr);
            session.adoptHostTree(nullptr);
            session.shutdown();
        });
        _guiWindows.shutdown();
        _app = nullptr;
        _playSession.shutdown(app);
        gEditorAuthoringScene = nullptr;
        app.removeSceneViewProducer(_viewProducer);
        _viewportCompositor.shutdown();
        _canvasCompositor.shutdown();
        _toolRecorder.destroy();
        if (_layer) {
            _layer->setViewportDisplayImage(nullptr);
            _layer->onDetach();
            _layer.reset();
        }
        gEditorLayer = nullptr;
        // WidgetTree chrome lazily built RuntimeDefault atlas textures; drop
        // them after the surface (snapshot/widget Font refs) is already gone.
        if (FontManager::get()) {
            FontManager::get()->clearCache();
        }
    }

    bool onBeforeAppStateChange(App& app, AppState previousState, AppState nextState) override
    {
        if (previousState == AppState::Stopped && nextState != AppState::Stopped) {
            return _playSession.begin(app, nextState);
        }
        if (previousState != AppState::Stopped && nextState == AppState::Stopped) {
            app.getInputRouter().cancelInput(EInputCancelReason::AppStateChanged);
            _playSession.end(app);
        }
        return true;
    }

    void onSceneActivated(App& app, Scene* scene) override
    {
        _playSession.onSceneActivated(app, scene);
        gEditorAuthoringScene = _playSession.getAuthoringScene();
        if (!_layer) {
            return;
        }

        const uint64_t selectedUUID = _layer->getSelectedEntityUUID();
        _layer->setEditableScene(_playSession.getAuthoringScene());
        _layer->setSceneContext(_layer->getViewportInteractionScene());
        Scene* const interactionScene = _layer->getViewportInteractionScene();
        _layer->selectEntity(interactionScene && selectedUUID != 0 ? interactionScene->getEntityByUUID(selectedUUID) : nullptr);
    }

    void onSceneDestroyed(App& app, Scene* scene) override
    {
        (void)app;
        _playSession.onSceneDestroyed(scene);
        gEditorAuthoringScene = _playSession.getAuthoringScene();
        if (_layer) {
            _layer->setEditableScene(_playSession.getAuthoringScene());
            _layer->setSceneContext(_layer->getViewportInteractionScene());
            _layer->selectEntity(nullptr);
        }
    }

    bool onEvent(App& app, const Event& event) override
    {
        (void)app;
        uint32_t mainId = 0;
        if (INativeWindow* native = mainNativeWindow()) {
            mainId = native->getWindowID();
        }
        _dragRouter.bindPrimary(mainId, _windows.defaultSession().tree(), mainNativeWindow());
        _dragRouter.bindExtras(&_guiWindows);
        _dragRouter.bindRender(app.getRenderServices().getRender());
        _dragRouter.adoptSource();
        if (_dragRouter.route(event)) {
            _dragRouter.sync();
            _dragRouter.syncTextInput();
            return true;
        }
        const uint32_t windowId = guiEventWindowId(event);
        if (windowId != 0 && _guiWindows.findSession(windowId)) {
            (void)_guiWindows.dispatchEvent(event);
            _dragRouter.adoptSource();
            _dragRouter.sync();
            return true;
        }
        if (event.getEventType() == EEvent::WindowFocus) {
            _guiWindows.setFocusedWindow(0);
        }
        return false;
    }

    // Per-frame GUI drive. Kernel order: onLogic → onViewportCompose →
    // onPresentation → recordExtraSurfaces (then the frame submits once).
    // See the file-level map above.

    void onLogic(App& app, float dt) override
    {
        if (!_layer) {
            return;
        }
        _layer->setSceneContext(_layer->getViewportInteractionScene());
        updateEditorCameraAndPrepareCompose(app, dt);
        _layer->onUpdate(dt);
    }

    void onViewportCompose(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)dt;
        if (!_layer) {
            return;
        }
        composeAuthoringViewport(app, commandBuffer);
        if (IRender* render = app.getRenderServices().getRender()) {
            _canvasCompositor.compose(*render, commandBuffer, _layer->getEditorUIDesignerSession());
        }
    }

    void onBeforePresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)app;
        (void)commandBuffer;
        (void)dt;
    }

    void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        if (!_layer) {
            return;
        }
        presentDefaultChrome(app, commandBuffer, dt);
    }

    void recordExtraSurfaces(App& app, float dt, FFrameSubmission& submission) override
    {
        (void)app;
        sweepAndRecordExtraWindows(dt, submission);
    }
};

} // namespace

std::unique_ptr<IModule> createEditorModule()
{
    return std::make_unique<EditorModule>();
}

EditorLayer* getEditorLayer()
{
    return gEditorLayer;
}

Scene* getEditorAuthoringScene()
{
    return gEditorAuthoringScene;
}

} // namespace ya
