#include "Editor/EditorModule.h"

#include "Editor/EditorLayer.h"
#include "Editor/Input/EditorInputNode.h"
#include "Editor/EditorPlaySession.h"
#include "Config/ConfigManager.h"
#include "Core/Camera/FreeCameraController.h"
#include "Core/Profiling/Profiling.h"
#include "Editor/EditorProfilingSettings.h"
#include "Editor/Inspector/TypeRenderer.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/Core/RenderImage.h"
#include "Resource/Font/FontManager.h"
#include "Resource/Texture/TextureLibrary.h"
#include "Runtime/Application/App.h"
#include "Runtime/GUI/GuiSystem.h"
#include "Runtime/Rendering/Common/Shadow/Common/ShadowSettingsConfig.h"
#include "Runtime/Rendering/RenderRuntime.h"

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
    auto&       editorCamera = layer.getCamera();
    const auto& desc         = app.getDesc();
    const float aspect       = desc.height > 0 ? static_cast<float>(desc.width) / static_cast<float>(desc.height) : (16.0f / 9.0f);
    editorCamera.setPerspective(45.0f, aspect, 0.1f, 100.0f);
    editorCamera.setPositionAndRotation(resolveInitialEditorCameraPosition(app),
                                        resolveInitialEditorCameraRotation(app));
}

std::shared_ptr<RenderImage> createEditorViewportImage(IRender& render, const RenderImage& source)
{
    const Extent2D extent = source.getExtent();
    if (extent.width == 0 || extent.height == 0) {
        return nullptr;
    }

    const EFormat::T targetFormat = source.getFormat() == EFormat::R16G16B16A16_SFLOAT
                                      ? source.getFormat()
                                      : EFormat::R16G16B16A16_SFLOAT;

    return createRenderImage(
        *render.getResourceFactory(),
        RenderImageDesc{
            .image = ImageCreateInfo{
                .label         = "EditorViewportComposed",
                .format        = targetFormat,
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

    void compose(IRender& render, ICommandBuffer& commandBuffer, const RenderViewportSnapshot& snapshot, const EditorLayer& layer)
    {
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

        const Extent2D extent = _composedViewportImage->getExtent();
        commandBuffer.beginRendering(RenderingInfo{
            .label                         = "EditorViewportComposition",
            .bExternalTransitionManagement = true,
            //
            .attachments = RenderAttachmentSet{
                .renderArea = Rect2D{
                    .pos    = {0.0f, 0.0f},
                    .extent = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
                },
                .layerCount = 1,
                .colors     = {

                    RenderAttachment{
                        .image         = _composedViewportImage->getImage(),
                        .imageView     = _composedViewportImage->getImageView(),
                        .loadOp        = EAttachmentLoadOp::Clear,
                        .storeOp       = EAttachmentStoreOp::Store,
                        .clearValue    = ClearValue(0.0f, 0.0f, 0.0f, 0.0f),
                        .initialLayout = EImageLayout::ColorAttachmentOptimal,
                        .finalLayout   = EImageLayout::ColorAttachmentOptimal,
                    },
                },
            },
        });

        FRender2dContext render2dCtx{
            .cmdBuf       = &commandBuffer,
            .windowWidth  = extent.width,
            .windowHeight = extent.height,
            .cam          = {
                         .position       = layer.getCamera().getPosition(),
                         .view           = layer.getCamera().getViewMatrix(),
                         .projection     = layer.getCamera().getProjectionMatrix(),
                         .viewProjection = layer.getCamera().getProjectionMatrix() * layer.getCamera().getViewMatrix(),
            },
        };
        Render2D::begin(render2dCtx);

        auto sourceTexture = resolveSourceTexture(*source);
        Render2D::makeSprite(glm::vec3(0.0f, 0.0f, 0.0f),
                             glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)),
                             sourceTexture.get(),
                             glm::vec4(1.0f));

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
        Render2D::onRender();
        Render2D::end();

        commandBuffer.endRendering();
        commandBuffer.transitionImageLayoutAuto(_composedViewportImage->getImage(), EImageLayout::ShaderReadOnlyOptimal);
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
};

class EditorModule final : public IModule
{
  private:
    std::unique_ptr<EditorLayer> _layer;
    EditorPlaySession            _playSession;
    FreeCameraController         _cameraController;
    EditorViewportCompositor     _viewportCompositor;
    EditorInputNode               _inputNode;
    InputRouter::FNodeRegistration _inputNodeRegistration;

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
        auto* renderRuntime = renderServices.getRenderRuntime();
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
        gEditorLayer = _layer.get();
    }

    void onDetach(App& app) override
    {
        app.getInputRouter().cancelInput(EInputCancelReason::ModuleDetached);
        _inputNodeRegistration.reset();
        _inputNode.unbind();
        _playSession.shutdown(app);
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

        auto& renderServices = app.getRenderServices();
        if (auto* renderRuntime = renderServices.getRenderRuntime()) {
            auto&          editorCamera   = _layer->getCamera();
            const Extent2D viewportExtent = renderRuntime->getViewportExtent();
            if (app.isStopped() && _layer->shouldCaptureInput()) {
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
        }

        _layer->onUpdate(dt);
        Rect2D pendingRect;
        if (_layer->getPendingViewportResize(pendingRect)) {
            if (auto* renderRuntime = renderServices.getRenderRuntime()) {
                renderRuntime->onViewportResized(pendingRect);
            }
        }
    }

    void onBeforePresentation(App& app, ICommandBuffer& commandBuffer, float dt) override
    {
        (void)dt;
        if (!_layer) {
            return;
        }

        auto& renderServices = app.getRenderServices();
        auto* renderRuntime = renderServices.getRenderRuntime();
        auto* render        = renderServices.getRender();
        if (!renderRuntime || !render) {
            _layer->setViewportDisplayImage(nullptr);
            return;
        }

        const auto snapshot = renderRuntime->buildViewportSnapshot();
        _layer->setViewportContext(snapshot);
        _viewportCompositor.compose(*render, commandBuffer, snapshot, *_layer);
        _layer->setViewportDisplayImage(_viewportCompositor.getOutputImage());
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

} // namespace ya
