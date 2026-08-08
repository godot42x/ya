#include "Product/Editor/EditorModule.h"

#include "Product/Host/Config/ConfigManager.h"
#include "Foundation/Core/Math/AABB.h"
#include "Framework/Game/Gameplay/ECS/System/CameraController/FreeCameraController.h"
#include "Foundation/Core/Profiling/Profiling.h"
#include "Framework/Game/Gameplay/ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Mesh/StaticMeshComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Material/PhongMaterialComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/Terrain/TerrainComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/PointLightComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/DirectionalLightComponent.h"
#include "Framework/Game/Gameplay/ECS/Component/TransformComponent.h"
#include "Framework/Game/Gameplay/ECS/System/TransformSystem.h"
#include "Product/Editor/EditorLayer.h"
#include "Product/Editor/EditorPlaySession.h"
#include "Product/Editor/EditorProfilingSettings.h"
#include "Product/Editor/EditorRuntimeSettings.h"
#include "Product/Editor/Input/EditorInputNode.h"
#include "Product/Editor/Inspector/TypeRenderer.h"
#include "Product/Editor/Services/NodeCreateRegistry.h"
#include "Framework/Game/Render/Render3D/Debug/PhysicsDebugDraw.h"
#include "Framework/GUI/Runtime/Draw2D/Render2D.h"
#include "Foundation/RHI/Core/CommandBuffer.h"
#include "Foundation/RHI/Core/RenderImage.h"
#include "Framework/GUI/Runtime/Resource/FontManager.h"
#include "Framework/GUI/Runtime/Resource/TextureLibrary.h"
#include "Product/Host/App.h"
#include "Product/Host/Automation/EditorAutomationControl.h"
#include "Product/Host/GUI/GuiSystem.h"
#include "Framework/Game/Render/Render3D/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Framework/GUI/Runtime/Compose/Render2DComposePass.h"
#include "Framework/Game/Render/Render3D/RenderRuntime.h"
#include "Framework/Game/Render/Render3D/Scene.h"
#include "Foundation/Core/Scripting/ScriptApiRegistry.h"

#include <format>
#include <string_view>

namespace ya
{

namespace
{

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

std::shared_ptr<RenderImage> createEditorViewportImage(IRender& render, const Extent2D& extent)
{
    if (extent.width == 0 || extent.height == 0) {
        return nullptr;
    }

    return createRenderImage(
        *render.getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = "EditorViewportComposed",
                .format        = EFormat::R16G16B16A16_SFLOAT,
                .extent        = {.width = extent.width, .height = extent.height, .depth = 1},
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = ESampleCount::Sample_1,
                .usage         = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                .initialLayout = EImageLayout::Undefined,
            },
            .defaultView = ImageViewCreateInfo{
                .label       = "EditorViewportComposed_DefaultView",
                .aspectFlags = EImageAspect::Color,
            },
        });
}

std::shared_ptr<RenderImage> createEditorViewportImage(IRender& render, const RenderImage& source)
{
    return createEditorViewportImage(render, source.getExtent());
}

void drawEntityBounds(Entity* entity, const glm::vec4& color)
{
    if (!entity || !entity->isValid()) {
        return;
    }
    Scene* scene = entity->getScene();
    if (!scene) {
        return;
    }

    if (!entity->hasComponent<TransformComponent>()) {
        return;
    }
    auto* tc = entity->getComponent<TransformComponent>();
    if (!tc) {
        return;
    }

    TransformSystem::computeWorldMatrix(tc);
    const glm::mat4 worldMatrix = tc->getWorldMatrix();

    const auto& registry = scene->getRegistry();
    const auto  handle   = entity->getHandle();

    AABB worldBounds;
    bool hasBounds = false;
    const auto addBounds = [&](const AABB& bounds) {
        if (bounds.max.x < bounds.min.x) {
            return;
        }
        worldBounds.merge(bounds.transformed(worldMatrix));
        hasBounds = true;
    };

    if (const auto* mesh = registry.try_get<StaticMeshComponent>(handle)) {
        if (auto* resolvedMesh = mesh->getMesh()) {
            addBounds(resolvedMesh->boundingBox);
        }
    }
    if (const auto* mesh = registry.try_get<SkinnedMeshComponent>(handle)) {
        if (auto* resolvedMesh = mesh->getMesh()) {
            addBounds(resolvedMesh->boundingBox);
        }
    }

    if (!hasBounds) {
        return;
    }

    Render2D::makeWireBox(glm::translate(glm::mat4(1.0f), worldBounds.getCenter()),
                          (worldBounds.max - worldBounds.min) * 0.5f,
                          color);
}

void drawSelectedEntityBounds(const EditorLayer& layer)
{
    const auto& selections = layer.getSelections();
    if (selections.empty()) {
        return;
    }

    // Primary selection stays at index 0 (see EditorLayer::setSelections).
    constexpr glm::vec4 kPrimarySelectionColor   = {0.98f, 0.69f, 0.23f, 1.0f};
    constexpr glm::vec4 kSecondarySelectionColor = {0.78f, 0.60f, 0.28f, 1.0f};

    for (size_t i = 0; i < selections.size(); ++i) {
        drawEntityBounds(selections[i], i == 0 ? kPrimarySelectionColor : kSecondarySelectionColor);
    }
}

class EditorViewportCompositor
{
  private:
    std::shared_ptr<RenderImage> _composedViewportImage   = nullptr;
    std::shared_ptr<Texture>     _sourceViewportTexture   = nullptr;
    std::shared_ptr<IImage>      _sourceViewportImage     = nullptr;
    std::shared_ptr<IImageView>  _sourceViewportImageView = nullptr;

  public:
    void shutdown()
    {
        _composedViewportImage.reset();
        _sourceViewportTexture.reset();
        _sourceViewportImage.reset();
        _sourceViewportImageView.reset();
    }

    [[nodiscard]] std::shared_ptr<RenderImage> getOutputImage() const
    {
        return _composedViewportImage;
    }

    void compose(IRender&                      render,
                 ICommandBuffer&               commandBuffer,
                 const RenderViewportSnapshot& snapshot,
                 const EditorLayer&            layer,
                 const AppRenderFrameState&    renderFrame,
                 Node*                         uiPreviewRoot,
                 const Extent2D&               canvasTargetExtent)
    {
        // 2D canvas preview does not consume the world output (the world scene
        // graph is disabled in this mode); create the target from the viewport
        // rect instead of the world image. 2D mode ALWAYS takes this path: with
        // the world graph disabled, falling through to the world-sourced
        // compose would leave the viewport with no image during the startup
        // frames before the editable scene is wired up (a null preview root
        // simply renders the grid without nodes).
        const bool bCanvasPreview = layer.isViewportMode2D();
        if (bCanvasPreview) {
            ensureCanvasTarget(render, canvasTargetExtent);
            if (!_composedViewportImage || !_composedViewportImage->isValid()) {
                return;
            }

            const glm::vec2 logicalViewport = layer.getViewportSize();
            recordRender2DComposePass(&commandBuffer,
                                      *_composedViewportImage,
                                      nullptr,
                                      uiPreviewRoot,
                                      FRender2DComposePassDesc{
                                          .kind = ERender2DComposePassKind::EditorCanvasPreview,
                                          .logicalViewportExtent = Extent2D{
                                              .width  = static_cast<uint32_t>(std::max(logicalViewport.x, 0.0f)),
                                              .height = static_cast<uint32_t>(std::max(logicalViewport.y, 0.0f)),
                                          },
                                          .canvasPan  = layer.getCanvasPan(),
                                          .canvasZoom = layer.getCanvasZoom(),
                                      });
            return;
        }

        auto source = snapshot.viewportImageOwner;
        if (!source || !source->getImageShared() || !source->getImageView()) {
            _composedViewportImage.reset();
            return;
        }

        ensureTarget(render, *source);
        if (!_composedViewportImage || !_composedViewportImage->isValid()) {
            return;
        }

        commandBuffer.retainResource(source->getImageShared());
        commandBuffer.retainResource(source->getImageViewShared());
        commandBuffer.retainResources(source->getRetainedResources());
        commandBuffer.retainResource(_composedViewportImage->getImageShared());
        commandBuffer.retainResource(_composedViewportImage->getImageViewShared());

        commandBuffer.transitionImageLayoutAuto(source->getImage(), EImageLayout::ShaderReadOnlyOptimal);
        commandBuffer.transitionImageLayoutAuto(_composedViewportImage->getImage(), EImageLayout::ColorAttachmentOptimal);

        // Attach the scene depth buffer when available so debug overlays
        // (collision wireframes) can be depth-tested against the world.
        const auto  depthOwner   = snapshot.viewportDepthOwner;
        const bool  bAttachDepth = depthOwner && depthOwner->isValid() &&
                                   depthOwner->getExtent() == _composedViewportImage->getExtent();
        if (bAttachDepth) {
            commandBuffer.retainResource(depthOwner->getImageShared());
            commandBuffer.retainResource(depthOwner->getImageViewShared());
            commandBuffer.retainResources(depthOwner->getRetainedResources());
            commandBuffer.transitionImageLayoutAuto(depthOwner->getImage(), EImageLayout::DepthStencilAttachmentOptimal);
        }

        recordRender2DComposePass(
            &commandBuffer,
            *_composedViewportImage,
            bAttachDepth ? depthOwner.get() : nullptr,
            nullptr,
            FRender2DComposePassDesc{
                .kind               = ERender2DComposePassKind::EditorViewportCompose,
                .sceneSourceTexture = resolveSourceTexture(*source),
                .camera             = {
                    .position       = renderFrame.cameraPos,
                    .view           = renderFrame.view,
                    .projection     = renderFrame.projection,
                    .viewProjection = renderFrame.projection * renderFrame.view,
                },
            },
            [&]() {
                // Camera overlay text on top of the composed viewport.
                const auto texts = layer.buildViewportCameraOverlayTexts();
                if (!texts.empty()) {
                    Render2D::makeSprite(glm::vec3(6.0f, 6.0f, 0.0f),
                                         glm::vec2(240.0f, 46.0f),
                                         TextureLibrary::get().getWhiteTexture().get(),
                                         glm::vec4(0.0f, 0.0f, 0.0f, 0.36f));
                }
                for (const auto& text : texts) {
                    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, text.fontSize);
                    if (!font) {
                        continue;
                    }
                    Render2D::makeText(text.text,
                                       glm::vec3(text.viewportPos, text.depth),
                                       text.color,
                                       font.get());
                }
                // Physics collision wireframes on top of the composed viewport,
                // depth-tested against the scene depth attached above.
                if (bAttachDepth) {
                    if (Scene* scene = layer.getViewportInteractionScene()) {
                        drawPhysicsCollisionDebug(*scene);
                    }
                    drawSelectedEntityBounds(layer);
                }
            });
    }

  private:
    std::shared_ptr<Texture> resolveSourceTexture(const RenderImage& source)
    {
        auto sourceImage     = source.getImageShared();
        auto sourceImageView = source.getImageViewShared();
        if (!sourceImage || !sourceImageView) {
            _sourceViewportTexture.reset();
            _sourceViewportImage.reset();
            _sourceViewportImageView.reset();
            return nullptr;
        }

        if (_sourceViewportTexture &&
            _sourceViewportImage == sourceImage &&
            _sourceViewportImageView == sourceImageView) {
            return _sourceViewportTexture;
        }

        _sourceViewportImage     = std::move(sourceImage);
        _sourceViewportImageView = std::move(sourceImageView);
        _sourceViewportTexture   = Texture::wrap(_sourceViewportImage,
                                               _sourceViewportImageView,
                                               "EditorViewportCompositionSource");
        return _sourceViewportTexture;
    }

    void ensureTarget(IRender& render, const RenderImage& source)
    {
        const Extent2D sourceExtent = source.getExtent();
        if (_composedViewportImage &&
            _composedViewportImage->getWidth() == sourceExtent.width &&
            _composedViewportImage->getHeight() == sourceExtent.height &&
            _composedViewportImage->getFormat() == (source.getFormat() == EFormat::R16G16B16A16_SFLOAT
                                                        ? source.getFormat()
                                                        : EFormat::R16G16B16A16_SFLOAT)) {
            return;
        }

        _composedViewportImage = createEditorViewportImage(render, source);
    }

    void ensureCanvasTarget(IRender& render, const Extent2D& extent)
    {
        if (_composedViewportImage &&
            _composedViewportImage->getWidth() == extent.width &&
            _composedViewportImage->getHeight() == extent.height &&
            _composedViewportImage->getFormat() == EFormat::R16G16B16A16_SFLOAT) {
            return;
        }

        _composedViewportImage = createEditorViewportImage(render, extent);
    }
};

class EditorModule final : public IModule, public IEditorAutomationControl
{
  private:
    std::unique_ptr<EditorLayer>   _layer;
    EditorPlaySession              _playSession;
    FreeCameraController           _cameraController;
    EditorViewportCompositor       _viewportCompositor;
    EditorInputNode                _inputNode;
    InputRouter::FNodeRegistration _inputNodeRegistration;
    bool                           _bWasRunning     = false;
    std::optional<EViewportMode>   _viewportModeBeforePlay;

    void registerEditorPresets()
    {
        auto& registry = editor::NodeCreateRegistry::get();

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
    }

    void registerEditorScriptApis()
    {
        using Json = ScriptApiRegistry::Json;
        auto& api  = ScriptApiRegistry::get();

        api.registerFunction(
            "viewport.set_mode",
            "Switches the editor viewport between '3d' (world) and '2d' (Node2D canvas preview).",
            Json{{"mode", {{"type", "string"}}}},
            [this](const Json& args) -> Json {
                const std::string mode = args.value("mode", "3d");
                if (mode == "2d") {
                    _layer->setViewportMode(EViewportMode::Mode2D);
                }
                else if (mode == "3d") {
                    _layer->setViewportMode(EViewportMode::Mode3D);
                }
                else {
                    throw ScriptApiRegistry::Error("viewport.set_mode: mode must be '3d' or '2d'");
                }
                return Json{{"mode", _layer->isViewportMode2D() ? "2d" : "3d"}};
            });

        api.registerFunction(
            "viewport.get_mode",
            "Returns the current editor viewport mode: {mode: '3d'|'2d'}.",
            Json::object(),
            [this](const Json&) -> Json {
                return Json{{"mode", _layer->isViewportMode2D() ? "2d" : "3d"}};
            });

        api.registerFunction(
            "viewport.pan_zoom",
            "Sets the 2D canvas preview navigation. Args: {pan_x?, pan_y?, zoom?}.",
            Json{{"pan_x", {{"type", "number"}}}, {"pan_y", {{"type", "number"}}}, {"zoom", {{"type", "number"}}}},
            [this](const Json& args) -> Json {
                if (args.contains("pan_x") || args.contains("pan_y")) {
                    glm::vec2 pan = _layer->getCanvasPan();
                    pan.x = args.value("pan_x", pan.x);
                    pan.y = args.value("pan_y", pan.y);
                    _layer->setCanvasPan(pan);
                }
                if (args.contains("zoom")) {
                    _layer->setCanvasZoom(args.at("zoom").get<float>());
                }
                return Json{{"pan_x", _layer->getCanvasPan().x},
                            {"pan_y", _layer->getCanvasPan().y},
                            {"zoom", _layer->getCanvasZoom()}};
            });

        api.registerFunction(
            "scene.create_preset",
            "Creates a node from the editor create registry. Args: {preset, name?, parent_path?}. "
            "Presets: Cube, Sphere, Plane, Terrain, Point Light, Directional Light, plus every Node2D type.",
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
                if (Node* presetNode = editor::NodeCreateRegistry::get().createPreset(preset, *scene, name, parent)) {
                    node = presetNode;
                }
                else {
                    // Fall back to reflection-driven Node2D types.
                    node = scene->createUINode(preset, name, parent);
                }
                if (!node) {
                    throw ScriptApiRegistry::Error(std::format("unknown create preset: {}", preset));
                }
                return Json{{"path", scene->getNodePath(node)}, {"name", node->getName()}};
            });
    }

  public:
    bool onLoad(FModuleContext&) override { return true; }
    bool onStart(const FEngineContext&) override { return true; }
    void onStop() override {}
    void onUnload() override {}

    void onConfigure(App& app, AppDesc& desc) override
    {
        ConfigManager::get().openDocument("editor", "Engine/Saved/Config/Editor.json");
        migrateLegacyRuntimeSettings();
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
    }

    void onAttach(App& app) override
    {
        auto& renderServices = app.getRenderServices();
        auto* renderRuntime  = renderServices.getRenderRuntime();
        YA_CORE_ASSERT(renderRuntime, "Editor extension requires an initialized RenderRuntime");

        GuiSystem::get().init(renderServices.getRender(), nullptr);
        registerBuiltinTypeRenderers();

        _layer = std::make_unique<EditorLayer>(&app);
        _layer->setCameraController(&_cameraController);
        initializeEditorCamera(app, *_layer);
        _layer->setCurrentScenePath(app.getDesc().defaultScenePath.value_or(std::string{}));
        _layer->onAttach();
        _inputNode.bind(app, *_layer);
        _inputNodeRegistration = app.getInputRouter().registerNode(_inputNode);
        gEditorLayer           = _layer.get();
        registerEditorPresets();
        registerEditorScriptApis();
    }

    void* queryInterface(FInterfaceId interfaceId) override
    {
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

    void onDetach(App& app) override
    {
        app.getInputRouter().cancelInput(EInputCancelReason::ModuleDetached);
        _inputNodeRegistration.reset();
        _inputNode.unbind();
        _playSession.shutdown(app);
        gEditorAuthoringScene = nullptr;
        app.getRenderServices().clearExtensionRenderFrameState();
        _viewportCompositor.shutdown();
        if (_layer) {
            _layer->setViewportDisplayImage(nullptr);
            _layer->onDetach();
            _layer.reset();
        }
        gEditorLayer = nullptr;
        GuiSystem::get().shutdown();
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
        (void)event;
        return false;
    }

    void onLogic(App& app, float dt) override
    {
        if (!_layer) {
            return;
        }

        // Entering runtime from the UI workspace mirrors Godot-style flow:
        // runtime starts in the 3D workspace, but the user may switch back to
        // the 2D authoring workspace while the play session keeps running.
        const bool bRunning = app.isRuntimeMode() || app.isSimulationMode();
        if (bRunning && !_bWasRunning && _layer->isViewportMode2D()) {
            _viewportModeBeforePlay = _layer->getViewportMode();
            _layer->setViewportMode(EViewportMode::Mode3D, /*bPersist=*/false);
        }
        else if (!bRunning && _bWasRunning && _viewportModeBeforePlay.has_value()) {
            _layer->setViewportMode(*_viewportModeBeforePlay, /*bPersist=*/false);
            _viewportModeBeforePlay.reset();
        }
        _bWasRunning = bRunning;
        _layer->setSceneContext(_layer->getViewportInteractionScene());

        auto& renderServices = app.getRenderServices();
        if (auto* renderRuntime = renderServices.getRenderRuntime()) {
            // The 2D canvas workspace only needs the UI compose pass and the
            // editor viewport panel; skip the whole world scene graph there.
            // PIE/sim already forced the viewport back to 3D above.
            renderRuntime->setWorldSceneRenderEnabled(!_layer->isViewportMode2D());

            auto&          editorCamera   = _layer->getCamera();
            const Extent2D viewportExtent = renderRuntime->getViewportExtent();
            // Keep the editor camera controllable during simulation; only full
            // runtime (PIE) hands viewport input over to the game. 2D canvas
            // preview uses its own pan/zoom navigation instead of the camera.
            if (!app.isRuntimeMode() && !_layer->isViewportMode2D() && _layer->shouldCaptureInput()) {
                _cameraController.update(editorCamera, app.getInputManager(), dt);
            }
            if (viewportExtent.height > 0) {
                editorCamera.setPerspective(editorCamera._fov,
                                            static_cast<float>(viewportExtent.width) / static_cast<float>(viewportExtent.height),
                                            editorCamera._nearClip,
                                            editorCamera._farClip);
            }
            renderServices.setExtensionRenderFrameState({
                .view       = editorCamera.getViewMatrix(),
                .projection = editorCamera.getProjectionMatrix(),
                .cameraPos  = editorCamera.getPosition(),
            });

            // The editor compositor always targets an HDR color image. Keep
            // the screen-space sprite pipeline's dynamic-rendering formats in
            // sync before presentation starts; recreating a pipeline while a
            // command buffer is recording invalidates that command buffer.
            const auto* activePipeline = renderRuntime->getActivePipeline();
            const EFormat::T depthFormat = activePipeline
                                               ? activePipeline->getViewportDepthFormat()
                                               : EFormat::Undefined;
            prepareRender2DComposePassPipeline(
                FRender2DComposePassDesc{
                    .kind = ERender2DComposePassKind::EditorViewportCompose,
                },
                EFormat::R16G16B16A16_SFLOAT,
                depthFormat);

            // The editor 2D canvas pass records into the composed viewport
            // image (always R16G16B16A16_SFLOAT); ensure the shared UI scene
            // pass exists outside command recording.
            if (_layer->isViewportMode2D()) {
                prepareRender2DComposePassPipeline(
                    FRender2DComposePassDesc{
                        .kind = ERender2DComposePassKind::EditorCanvasPreview,
                    },
                    EFormat::R16G16B16A16_SFLOAT);
            }
        }

        _layer->onUpdate(dt);
        Rect2D pendingRect;
        if (_layer->getPendingViewportResize(pendingRect)) {
            if (auto* renderRuntime = renderServices.getRenderRuntime()) {
                renderRuntime->onViewportResized(pendingRect);
            }
        }
    }

    void onViewportCompose(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)dt;
        if (!_layer) {
            return;
        }

        auto& renderServices = app.getRenderServices();
        auto* renderRuntime  = renderServices.getRenderRuntime();
        auto* render         = renderServices.getRender();
        if (!renderRuntime || !render) {
            _layer->setViewportDisplayImage(nullptr);
            return;
        }

        const auto snapshot = renderRuntime->buildViewportSnapshot();
        _layer->setViewportContext(snapshot);
        _layer->setEntityIdPickImage(snapshot.entityIdImageOwner);
        // 2D preview composites the authoring scene's Node2D tree over a grid;
        // during PIE/sim the runtime already composites UI, so the editor
        // preview stays in 3D presentation (no double-draw).
        Node* uiPreviewRoot = nullptr;
        if (_layer->isViewportMode2D()) {
            if (Scene* scene = _layer->getViewportInteractionScene()) {
                uiPreviewRoot = scene->getRootNode();
            }
        }
        // 2D mode disables the world scene graph, so the runtime pipeline never
        // publishes viewport resources and getViewportExtent() stays 0x0;
        // size the canvas target from the editor panel instead (same fallback
        // guards a degenerate pipeline extent in 3D).
        Extent2D canvasTargetExtent = renderRuntime->getViewportExtent();
        if (_layer->isViewportMode2D() ||
            canvasTargetExtent.width == 0 || canvasTargetExtent.height == 0) {
            canvasTargetExtent = Extent2D::fromVec2(_layer->getViewportSize());
        }
        _viewportCompositor.compose(*render,
                                    commandBuffer,
                                    snapshot,
                                    *_layer,
                                    app.getRenderServices().getRenderFrameState(),
                                    uiPreviewRoot,
                                    canvasTargetExtent);
        // Keep the last valid frame instead of clobbering the display with a
        // transiently null output (startup / mode-switch / resize gaps).
        if (auto output = _viewportCompositor.getOutputImage();
            output && output->isValid() && output->getImageView()) {
            _layer->setViewportDisplayImage(std::move(output));
        }
    }

    void onPresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)dt;
        if (!_layer) {
            return;
        }

        GuiSystem::get().beginFrame();
        _layer->onImGuiRender();
        GuiSystem::get().endFrame();
        GuiSystem::get().render();
        GuiSystem::get().submit(commandBuffer);
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
