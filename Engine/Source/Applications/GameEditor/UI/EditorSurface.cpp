#include "GameEditor/UI/EditorSurface.h"
#include "GameEditor/UI/EditorSurfaceContext.h"
#include "GameEditor/UI/EditorActionCatalog.h"
#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorFilePicker.h"
#include "GameEditor/UI/EditorFilePickerDialog.h"
#include "GameEditor/UI/EditorSettingsDialog.h"
#include "GameEditor/UI/EditorInspectorTab.h"

#include "Core/Event.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "GameEditor/UI/EditorViewportHost.h"
#include "GameEditor/UI/EditorViewportGizmoOverlay.h"
#include "GameEditor/UI/EditorListRows.h"
#include "GUI/Declarative/Build.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "GameRuntime/App.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Host/OsClipboard.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/WidgetAttachment.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/System/PathUtils.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/CommandBuffer.h"
#include "Render/Resources/FontManager.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace ya
{

struct FEditorProjectBrowser
{
    std::shared_ptr<UITreeView> list;
    std::shared_ptr<ReactiveList<UITreeView::FNode>> roots;
    std::shared_ptr<UIText> errorText;
};

namespace
{

constexpr float kMenuHeight     = editor_density::kMenuHeight;
constexpr float kToolbarHeight  = editor_density::kToolbarHeight;
constexpr float kChromeTop      = kMenuHeight + kToolbarHeight;

std::vector<UIMenu::FItem> makePresetMenuItems(EditorLayer& layer, const std::string& category)
{
    std::vector<UIMenu::FItem> items;
    for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
        if (entry.category != category) {
            continue;
        }
        const std::string presetName = entry.displayName;
        items.push_back({
            .label  = presetName,
            .action = [&layer, presetName]() { layer.cmdCreateNodePreset(presetName); },
        });
    }
    return items;
}

} // namespace

EditorSurface::EditorSurface() = default;

EditorSurface::~EditorSurface()
{
    unbindAppState();
}

void EditorSurface::shutdown()
{
    unbindAppState();
    closeViewportContextMenu();
    _bViewportRightPressPending = false;
    if (_filePicker) {
        _filePicker->reset();
    }
    _filePicker.reset();
    if (_settings) {
        _settings->reset();
    }
    _settings.reset();
    _tree.reset();
    _theme.reset();
    _snapshot = {};
    _root.reset();
    _menuBar.reset();
    _toolbarModeText.reset();
    _workspace.clear();
    _dockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _viewportHost = nullptr;
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _projectBrowser.reset();
    _selection = std::make_shared<SelectionModel>();
    _actions   = std::make_shared<ActionMap>();
    _undo      = std::make_shared<UndoStack>();
    _viewportTexture.reset();
    _viewportImageResource.reset();
    _viewportImageView.reset();
    _layer = nullptr;
    _tabSpawners = nullptr;
}

void EditorSurface::tick(App& app, float dt)
{
    const FEditorSurfaceContext context = makeEditorSurfaceContext(app);
    if (!_layer) {
        return;
    }

    const bool bProjectLoaded = _layer->isProjectLoaded();
    if (!_tree || _bBuiltAsProjectBrowser == bProjectLoaded) {
        rebuild(app, context.metrics);
    }

    tick(context, dt);
}

void EditorSurface::tick(const FEditorSurfaceContext& context, float dt)
{
    if (!_layer || !_tree) {
        return;
    }

    applyWindowMetrics(context.metrics);
    _tree->tick(dt);
    syncShellDialogs();
    pushViewportDisplay();
    UIFrameBuildContext snapshotCtx;
    snapshotCtx.textureResolver = &resolveGameUITexture;
    _snapshot = _tree->buildSnapshot(snapshotCtx);
    publishViewportRect();
    syncViewportHostState(context);
}

void EditorSurface::rebuild(App& app, const EditorWindowMetrics& metrics)
{
    unbindAppState();
    closeViewportContextMenu();
    _bViewportRightPressPending = false;
    _root.reset();
    _menuBar.reset();
    _toolbarModeText.reset();
    _workspace.clear();
    _dockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _viewportHost = nullptr;
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _projectBrowser.reset();
    _selection = std::make_shared<SelectionModel>();
    _actions   = std::make_shared<ActionMap>();
    _undo      = std::make_shared<UndoStack>();
    if (_filePicker) {
        _filePicker->reset();
    }
    _filePicker.reset();
    if (_settings) {
        _settings->reset();
    }
    _settings.reset();

    _tree = std::make_unique<WidgetTree>(Extent2D{
        .width  = std::max(metrics.logicalExtent.width, 1u),
        .height = std::max(metrics.logicalExtent.height, 1u),
    });
    _tree->setDpiScale(metrics.dpiScale > 0.0f ? metrics.dpiScale : 1.0f);
    _tree->setTextureSource(&gameUITextureSource());
    bindSdlClipboard(*_tree);
    _theme = buildEditorTheme(true);
    _tree->setTheme(_theme.get());

    if (_layer->isProjectLoaded()) {
        _bBuiltAsProjectBrowser = false;
        buildEditorChrome(app);
    }
    else {
        _bBuiltAsProjectBrowser = true;
        buildProjectBrowser(app);
    }
}

void EditorSurface::buildProjectBrowser(App& app)
{
    (void)app;
    _layer->requestRefreshProjectBrowser();

    auto title = ui::text("ProjectTitle").setText("YA Editor").setStyleKey("text.header");
    auto blurb = ui::text("ProjectBlurb")
                     .setText("Select a project to open. The WidgetTree chrome stays isolated until a project is loaded.")
                     .setStyleKey("text.muted");

    auto refresh = labeledButton("RefreshProjects", "Refresh Projects")
                       .setOnClick([this]() {
                           _layer->requestRefreshProjectBrowser();
                           refreshProjectBrowserRows();
                       });
    auto exitBtn = labeledButton("ExitEditor", "Exit Editor")
                       .setOnClick([]() {
                           if (auto* app = App::get()) {
                               app->requestQuit();
                           }
                       });

    _projectBrowser = std::make_unique<FEditorProjectBrowser>();
    _projectBrowser->roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    auto list = ui::treeView("ProjectList")
                    .bindData(_projectBrowser->roots)
                    .bindSelection(_selection->primaryRef())
                    .setOnSelectionChanged([this](const std::string& id) {
                        _selection->select(id);
                        int index = 0;
                        if (auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), index);
                            ec == std::errc{} && ptr == id.data() + id.size()) {
                            _layer->setProjectBrowserSelection(index);
                        }
                    });
    _projectBrowser->list = list.share();

    auto openBtn = labeledButton("OpenProject", "Open Project")
                       .setOnClick([this]() {
                           const auto& projects = _layer->getDiscoveredProjects();
                           const int   index    = _layer->getProjectBrowserSelection();
                           if (index >= 0 && index < static_cast<int>(projects.size())) {
                               _layer->requestOpenProject(projects[static_cast<size_t>(index)]);
                               refreshProjectBrowserRows();
                           }
                       });

    auto errorText = ui::text("ProjectError").setStyleKey("text.error");
    _projectBrowser->errorText = errorText.share();

    auto page = ui::column("ProjectBrowser")
                    .setPadding({48.0f, 48.0f})
                    .setSpacing(12.0f)
                    .child(std::move(title))
                    .child(std::move(blurb), ui::boxSlot().preferredSize({720.0f, 40.0f}))
                    .child(ui::row("ProjectActions")
                                  .setSpacing(8.0f)
                                  .child(std::move(refresh), ui::boxSlot().preferredSize({160.0f, 26.0f}))
                                  .child(std::move(exitBtn), ui::boxSlot().preferredSize({160.0f, 26.0f})))
                    .child(std::move(list), ui::boxSlot().preferredSize({720.0f, 320.0f}))
                    .child(std::move(openBtn), ui::boxSlot().preferredSize({160.0f, 26.0f}))
                    .child(std::move(errorText));
    ui::build(*_tree, *_tree->getLayer(WidgetTree::ELayer::Content), std::move(page), ui::canvasSlot().fill());
    refreshProjectBrowserRows();
}

void EditorSurface::buildEditorChrome(App& app)
{
    (void)app;
    registerEditorActions(*_actions,
                          *_layer,
                          *_undo,
                          [this]() { openSceneSaveDialog(); },
                          [this]() { openEditorSettingsDialog(); });
    _root = ui::panel("EditorRoot").setStyleKey("panel.window").share();
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const WidgetAttachment attached = _tree->attachToLayer(WidgetTree::ELayer::Content, _root, fillArgs);
    YA_CORE_ASSERT(attached.valid(), "EditorSurface: failed to attach editor root");

    _menuBar = ui::buildAs<UIMenuBar>(*_tree,
                                      *_root,
                                      ui::menuBar("EditorMenu"),
                                      ui::canvasSlot()
                                          .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                                          .size({0.0f, kMenuHeight}));

    _menuBar->addItem("File", [this]() {
        return UIMenu::create({
            UIMenu::FItem::fromAction(*_actions, "scene.new"),
            UIMenu::FItem::fromAction(*_actions, "scene.save"),
            UIMenu::FItem::fromAction(*_actions, "scene.saveAs"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(*_actions, "app.exit"),
        });
    });
    _menuBar->addItem("Edit", [this]() {
        return UIMenu::create({
            UIMenu::FItem::fromAction(*_actions, "edit.undo"),
            UIMenu::FItem::fromAction(*_actions, "edit.redo"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(*_actions, "selection.duplicate"),
            UIMenu::FItem::fromAction(*_actions, "selection.delete"),
        });
    });
    _menuBar->addItem("View", [this]() {
        std::vector<UIMenu::FItem> items = {
            UIMenu::FItem::fromAction(*_actions, "viewport.mode3d"),
            UIMenu::FItem::fromAction(*_actions, "viewport.mode2d"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(*_actions, "editor.settings"),
        };
#if !defined(YA_PROFILING_DISABLED)
        auto channelItem = [](const char* label, EGuiFrameInspectorChannel channel) {
            UIMenu::FItem item;
            item.label     = label;
            item.bCheckable = true;
            item.bChecked  = isGuiFrameInspectorChannelOn(channel);
            item.action   = [channel]() { toggleGuiFrameInspectorChannel(channel); };
            return item;
        };
        items.push_back(UIMenu::FItem::separator());
        items.push_back(channelItem("Frame Inspector HUD", EGuiFrameInspectorChannel::Hud));
        items.push_back(channelItem("Rebuild Flash", EGuiFrameInspectorChannel::Rebuild));
        items.push_back(channelItem("Overdraw Heatmap", EGuiFrameInspectorChannel::Overdraw));
#endif
        return UIMenu::create(std::move(items));
    });

    auto play = iconLabeledButton("Play", "Play", editor_icons::kPlay).setOnClick([this]() {
        (void)_actions->execute("runtime.play");
    });
    auto simulate = iconLabeledButton("Simulate", "Simulate", editor_icons::kSimulate).setOnClick([this]() {
        (void)_actions->execute("runtime.simulate");
    });
    auto stop = iconLabeledButton("Stop", "Stop", editor_icons::kStop).setOnClick([this]() {
        (void)_actions->execute("runtime.stop");
    });
    auto mode3d = labeledButton("Mode3D", "3D").setOnClick([this]() {
        (void)_actions->execute("viewport.mode3d");
    });
    auto mode2d = labeledButton("Mode2D", "2D").setOnClick([this]() {
        (void)_actions->execute("viewport.mode2d");
    });
    auto modeText = ui::text("ToolbarMode").setFontSize(13).setText("EDIT");
    _toolbarModeText = modeText.share();

    ui::build(*_tree,
              *_root,
              ui::row("EditorToolbar")
                  .setSpacing(6.0f)
                  .setPadding({6.0f, 2.0f})
                  .child(std::move(play), ui::boxSlot().preferredSize({64.0f, 22.0f}))
                  .child(std::move(simulate), ui::boxSlot().preferredSize({80.0f, 22.0f}))
                  .child(std::move(stop), ui::boxSlot().preferredSize({64.0f, 22.0f}))
                  .child(std::move(mode3d), ui::boxSlot().preferredSize({40.0f, 22.0f}))
                  .child(std::move(mode2d), ui::boxSlot().preferredSize({40.0f, 22.0f}))
                  .child(std::move(modeText), ui::boxSlot().preferredSize({72.0f, 18.0f})),
              ui::canvasSlot()
                  .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                  .offset({0.0f, kMenuHeight})
                  .size({0.0f, kToolbarHeight}));

    _dockContext = std::make_shared<FDockContext>();
    _dockContext->bAllowFloating = true;
    _dockContext->bAllowTearOff  = true;
    _dockSpace = ui::buildAs<UIDockSpace>(*_tree,
                                          *_root,
                                          ui::dockSpace("EditorDock").setContext(_dockContext),
                                          ui::canvasSlot()
                                              .anchor({0.0f, 0.0f}, {1.0f, 1.0f})
                                              .insets(FMargin{editor_density::kChromeInset,
                                                              kChromeTop,
                                                              editor_density::kChromeInset,
                                                              editor_density::kChromeInset}));

    _dockFloatingHost = std::make_shared<UIDockFloatingHost>("EditorDockFloatingHost");
    _dockFloatingHost->bindContext(_dockContext);
    FCanvasSlotArgs floatingFill;
    floatingFill.anchorMin = {0.0f, 0.0f};
    floatingFill.anchorMax = {1.0f, 1.0f};
    (void)_tree->attachToLayer(WidgetTree::ELayer::Popup, _dockFloatingHost, floatingFill);

    _workspace.bind(EditorDockWorkspace::FHost{
        .tree            = _tree.get(),
        .layer           = _layer,
        .selection       = _selection.get(),
        .actions         = _actions.get(),
        .undo            = _undo.get(),
        .viewportHost    = this,
        .spawners        = _tabSpawners,
        .dock            = _dockContext.get(),
        .menuBar         = _menuBar.get(),
    });
    _workspace.buildWindowMenu();
    _workspace.applyWorkspaceLayout();
    _dockContext->fireDockUpdated();
    _dockContext->appendOnDockUpdated([this]() { _workspace.persistLayout(); });
    _dockContext->appendOnFloatingUpdated([this]() { _workspace.persistLayout(); });

    if (const std::optional<std::string>& editorTab = app.getDesc().editorTab; editorTab && !editorTab->empty()) {
        _workspace.invokeTab(*editorTab);
    }

    _viewportGizmoOverlay = std::make_shared<EditorViewportGizmoOverlay>(_layer->gizmo());
    _viewportOverlayHost.setOverlay(_viewportGizmoOverlay);
    _layer->gizmo().setUndoStack(_undo.get());
    bindAppState(app);
    updateToolbarMode(app);
}

void EditorSurface::applyWindowMetrics(const EditorWindowMetrics& metrics)
{
    if (!_tree) {
        return;
    }
    applyEditorWindowMetrics(*_tree, metrics);
    if (auto* fonts = FontManager::get()) {
        fonts->setActiveDpiScale(_tree->getDpiScale());
    }
}

void EditorSurface::closeViewportContextMenu()
{
    if (!_viewportContextMenu) {
        return;
    }
    std::shared_ptr<UIMenu> menu = std::move(_viewportContextMenu);
    menu->_onDismiss             = nullptr;
    menu->close();
}

void EditorSurface::openViewportContextMenu(const glm::vec2& windowPoint)
{
    if (!_tree || !_layer || !_layer->canViewportAuthor()) {
        return;
    }

    closeViewportContextMenu();

    EditorLayer& layer = *_layer;
    std::vector<UIMenu::FItem> items;
    items.push_back(UIMenu::FItem::fromAction(*_actions, "selection.createEmpty"));
    items.push_back({
        .label          = "Create 3D Object",
        .submenuFactory = [&layer]()
        {
            return UIMenu::create(makePresetMenuItems(layer, "3D Object"));
        },
    });

    for (const editor::NodeCreateEntry& entry : editor::NodeCreateRegistry::get().presets()) {
        if (entry.category != "Light") {
            continue;
        }
        const std::string presetName = entry.displayName;
        items.push_back({
            .label  = std::format("Create {}", presetName),
            .action = [&layer, presetName]() { layer.cmdCreateNodePreset(presetName); },
        });
    }

    items.push_back(UIMenu::FItem::separator());
    items.push_back(UIMenu::FItem::fromAction(*_actions, "selection.duplicate"));
    items.push_back(UIMenu::FItem::fromAction(*_actions, "selection.delete"));

    auto menu = UIMenu::create(std::move(items));
    _viewportContextMenu = menu;
    menu->_onDismiss = [this]() { _viewportContextMenu.reset(); };
    menu->openAt(*_tree, windowPoint);
}

void EditorSurface::syncShellDialogs()
{
    if (!_tree) {
        return;
    }
    if (_filePicker) {
        _filePicker->sync(*_tree);
    }
    if (_settings) {
        _settings->sync(*_tree);
    }
}

void EditorSurface::refreshProjectBrowserRows()
{
    if (!_layer) {
        return;
    }
    std::vector<UITreeView::FNode> projects;
    const auto& discovered = _layer->getDiscoveredProjects();
    projects.reserve(discovered.size());
    for (int i = 0; i < static_cast<int>(discovered.size()); ++i) {
        projects.push_back(UITreeView::FNode{
            .id    = std::to_string(i),
            .label = discovered[static_cast<size_t>(i)],
        });
    }
    if (_projectBrowser && _projectBrowser->roots) {
        _projectBrowser->roots->replace(std::move(projects));
    }
    if (_projectBrowser && _projectBrowser->errorText) {
        _projectBrowser->errorText->setText(_layer->getProjectBrowserError());
    }
}

void EditorSurface::bindAppState(App& app)
{
    unbindAppState();
    _app = &app;
    _appStateHandle = app.onAppStateChanged.addLambda(this, [this](AppState) {
        if (_app) {
            updateToolbarMode(*_app);
        }
    });
}

void EditorSurface::unbindAppState()
{
    if (_app && _appStateHandle != INVALID_HANDLE) {
        _app->onAppStateChanged.remove(_appStateHandle);
    }
    _appStateHandle = INVALID_HANDLE;
    _app = nullptr;
}

void EditorSurface::pushViewportDisplay()
{
    if (!_viewportHost || !_layer) {
        return;
    }
    const auto& display = _layer->getViewportDisplayImage();
    const bool expectsViewport = _layer->getHierarchyScene() != nullptr;
    if (!display || !display->isValid() || !display->getImageView()) {
        _viewportHost->setDisplayImage(nullptr, expectsViewport);
        _viewportTexture.reset();
        _viewportImageResource.reset();
        _viewportImageView.reset();
        return;
    }

    auto sourceImage     = display->getImageShared();
    auto sourceImageView = display->getImageViewShared();
    if (!sourceImage || !sourceImageView) {
        _viewportHost->setDisplayImage(nullptr, expectsViewport);
        return;
    }
    if (_viewportTexture &&
        _viewportImageResource == sourceImage &&
        _viewportImageView == sourceImageView) {
        _viewportHost->setDisplayImage(_viewportTexture, false);
        return;
    }

    _viewportImageResource = std::move(sourceImage);
    _viewportImageView     = std::move(sourceImageView);
    _viewportTexture       = Texture::wrap(_viewportImageResource,
                                     _viewportImageView,
                                     "EditorSurfaceViewport");
    _viewportHost->setDisplayImage(_viewportTexture, false);
}

void EditorSurface::updateToolbarMode(App& app)
{
    if (!_toolbarModeText) {
        return;
    }
    const char* label = app.isRuntimeMode() ? "PLAYING"
                        : app.isSimulationMode() ? "SIMULATING"
                                                 : "EDIT";
    _toolbarModeText->setText(label);
}

void EditorSurface::openSceneSaveDialog()
{
    if (!_tree || !_root || !_layer) {
        return;
    }

    std::string defaultName = "NewScene";
    if (Scene* scene = _layer->getHierarchyScene(); scene && !scene->getName().empty()) {
        defaultName = scene->getName();
    }
    std::string currentPath;
    if (!_layer->getCurrentScenePath().empty()) {
        currentPath = path_utils::pathToUtf8String(
            std::filesystem::path(_layer->getCurrentScenePath()).parent_path());
    }

    openFilePickerDialog(makeSceneSavePickerRequest(
        std::move(defaultName),
        std::move(currentPath),
        [this](std::string scenePath) {
            if (!_layer) {
                return;
            }
            _layer->setCurrentScenePath(scenePath);
            if (Scene* scene = _layer->getEditableScene()) {
                scene->setName(std::filesystem::path(scenePath).stem().string());
            }
            if (App* app = App::get()) {
                app->getSceneServices().saveScene(scenePath);
            }
            YA_CORE_INFO("Scene saved to: {}", scenePath);
        }));
}

void EditorSurface::openAssetPickerDialog(EEditorAssetPickerKind kind,
                                          std::string currentPath,
                                          std::function<void(std::string)> onPicked)
{
    openFilePickerDialog(makeAssetPickerRequest(kind, std::move(currentPath), std::move(onPicked)));
}

void EditorSurface::showContentBrowser()
{
    (void)_workspace.invokeTab("content-browser");
}

void EditorSurface::openFilePickerDialog(FEditorFilePickerRequest request)
{
    if (!_tree || !_root) {
        return;
    }
    if (!_filePicker) {
        _filePicker = std::make_unique<EditorFilePickerDialog>();
    }
    _filePicker->open(*_tree, std::move(request));
}

void EditorSurface::openEditorSettingsDialog()
{
    if (!_tree || !_root || !_layer) {
        return;
    }
    if (!_settings) {
        _settings = std::make_unique<EditorSettingsDialog>();
    }
    EditorLayer* layer = _layer;
    _settings->open(*_tree, FEditorSettingsBindings{
        .samplerIndex = [layer]() { return layer->getViewportSamplerType(); },
        .setSamplerIndex = [layer](int index) { layer->setViewportSamplerType(index); },
        .showCameraOverlay = [layer]() { return layer->shouldShowViewportCameraOverlay(); },
        .setShowCameraOverlay = [layer](bool enabled) { layer->setShowViewportCameraOverlay(enabled); },
        .scenePathDraft = [layer]() { return layer->getDefaultScenePathDraft(); },
        .setScenePathDraft = [layer](std::string path) { layer->setDefaultScenePathDraft(std::move(path)); },
        .scenePathDirty = [layer]() { return layer->isDefaultScenePathDirty(); },
        .scenePathExists = [layer]() { return layer->defaultScenePathExists(); },
        .applyScenePath = [layer]() { layer->applyDefaultScenePathDraft(); },
        .resetScenePath = [layer]() { layer->resetDefaultScenePathDraft(); },
        .openFilePicker = [this](FEditorFilePickerRequest request) {
            openFilePickerDialog(std::move(request));
        },
    });
}

void EditorSurface::publishViewportRect()
{
    if (!_viewportHost || !_layer) {
        return;
    }
    const Rect2D rect = _viewportHost->imageRect();
    _layer->notifyViewportWidgetRect(rect);
    const bool hovered = _viewportHost->isHovered();
    const bool focused = _viewportHost->isFocused() || hovered;
    _layer->setViewportHoverFocus(hovered, focused);
}

void EditorSurface::syncViewportHostState(const FEditorSurfaceContext& context)
{
    if (!_viewportHost) {
        return;
    }

    FEditorViewportHostState state{};
    state.widgetRect = _viewportHost->imageRect();
    state.extent     = state.widgetRect.extent;
    state.bHovered   = isViewportHovered();
    state.bFocused   = isViewportFocused();
    state.view       = context.view;
    state.projection = context.projection;

    _viewportOverlayHost.syncHost(state);
}

EWidgetRouteResult EditorSurface::dispatchEvent(const Event& event, const glm::vec2& windowPoint)
{
    if (!_tree) {
        return EWidgetRouteResult::NotHandled;
    }

    bool bPopupOpen = false;
    if (UIElement* popup = _tree->getLayer(WidgetTree::ELayer::Popup)) {
        bPopupOpen = !popup->getChildren().empty();
    }
    if (handleGuiFrameInspectorHudInput(event, windowPoint, _tree->getLogicalExtent(), bPopupOpen)) {
        return EWidgetRouteResult::HandledExclusive;
    }

    constexpr float kViewportContextDragSlop = 4.0f;
    const bool overlayCandidate =
        _viewportHost &&
        (isPointInViewport(windowPoint) || _viewportOverlayHost.wantsPointerCapture() ||
         _bViewportRightPressPending);
    if (overlayCandidate) {
        const glm::vec2 localPoint = windowPoint - _viewportHost->imageRect().pos;
        const EWidgetRouteResult overlayResult = _viewportOverlayHost.dispatchEvent(event, localPoint);
        if (overlayResult != EWidgetRouteResult::NotHandled) {
            _bViewportRightPressPending = false;
            return overlayResult;
        }
    }

    WidgetEventContext ctx;
    ctx.logicalPoint = windowPoint;
    const EWidgetRouteResult routed = _tree->dispatchEvent(event, ctx);

    if (event.getEventType() == EEvent::MouseButtonPressed && isPointInViewport(windowPoint) &&
        _viewportHost &&
        (routed != EWidgetRouteResult::HandledExclusive || isViewportHovered())) {
        _viewportHost->takeKeyboardFocus();
    }

    const bool canAuthor = _layer && _layer->canViewportAuthor();
    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed: {
        const auto& mouseEvent = static_cast<const MouseButtonPressedEvent&>(event);
        if (mouseEvent.GetMouseButton() != EMouse::Right) {
            break;
        }
        _bViewportRightPressPending = false;
        if (canAuthor && isPointInViewport(windowPoint)) {
            _bViewportRightPressPending = true;
            _viewportRightPressPos      = windowPoint;
        }
        if (routed == EWidgetRouteResult::HandledExclusive && !isViewportHovered()) {
            _bViewportRightPressPending = false;
        }
        break;
    }
    case EEvent::MouseMoved: {
        if (_bViewportRightPressPending &&
            glm::length(windowPoint - _viewportRightPressPos) > kViewportContextDragSlop) {
            _bViewportRightPressPending = false;
        }
        break;
    }
    case EEvent::MouseButtonReleased: {
        const auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
        if (mouseEvent.GetMouseButton() != EMouse::Right) {
            break;
        }
        const bool openMenu =
            _bViewportRightPressPending && canAuthor && isPointInViewport(windowPoint);
        _bViewportRightPressPending = false;
        if (openMenu) {
            openViewportContextMenu(windowPoint);
            return EWidgetRouteResult::HandledExclusive;
        }
        break;
    }
    default:
        break;
    }

    if (routed != EWidgetRouteResult::NotHandled) {
        return routed;
    }
    if (event.getEventType() == EEvent::KeyPressed &&
        _actions->dispatchKey(static_cast<const KeyPressedEvent&>(event), wantsTextInput())) {
        return EWidgetRouteResult::HandledExclusive;
    }
    return routed;
}

bool EditorSurface::isPointInViewport(const glm::vec2& windowPoint) const
{
    if (!_viewportHost) {
        return false;
    }
    const Rect2D rect = _viewportHost->imageRect();
    return windowPoint.x >= rect.pos.x && windowPoint.x < rect.pos.x + rect.extent.x &&
           windowPoint.y >= rect.pos.y && windowPoint.y < rect.pos.y + rect.extent.y;
}

bool EditorSurface::isViewportHovered() const
{
    return _viewportHost && _viewportHost->isHovered();
}

bool EditorSurface::isViewportFocused() const
{
    return _viewportHost && _viewportHost->isFocused();
}

bool EditorSurface::wantsTextInput() const
{
    if (!_tree) {
        return false;
    }
    UIElement* focused = _tree->getFocused();
    if (dynamic_cast<UITextField*>(focused) != nullptr) {
        return true;
    }
    if (_dockContext) {
        if (const FDockContext::FPanel* inspector = _dockContext->findPanelByStableKey("inspector")) {
            if (auto* tab = dynamic_cast<EditorInspectorTab*>(inspector->widget.get())) {
                return tab->wantsTextInput();
            }
        }
    }
    return false;
}

} // namespace ya
