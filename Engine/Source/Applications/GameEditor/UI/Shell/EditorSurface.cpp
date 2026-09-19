#include "GameEditor/UI/Shell/EditorSurface.h"
#include "GameEditor/UI/Shell/EditorSurfaceContext.h"
#include "GameEditor/UI/Shell/EditorRootSession.h"
#include "GameEditor/UI/Shell/EditorActionCatalog.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"
#include "GameEditor/UI/Dialogs/EditorFilePicker.h"
#include "GameEditor/UI/Dialogs/EditorFilePickerDialog.h"
#include "GameEditor/UI/Dialogs/EditorSettingsDialog.h"
#include "GameEditor/UI/Shell/EditorTabSpawnerRegistry.h"

#include "Core/Config/ConfigManager.h"
#include "Core/Event.h"
#include "Core/Log.h"
#include "Core/Profiling/Profiling.h"
#include "GameEditor/UI/Viewport/EditorViewportHost.h"
#include "GameEditor/UI/Viewport/EditorViewportGizmoOverlay.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GUI/Declarative/Build.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/EditorViewProducer.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "GameRuntime/App.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Host/GUIWindowChrome.h"
#include "GUI/Host/OsClipboard.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/Controls/DockSpace/DockNode.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/GuiFrameInspector.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/UIBehavior.h"
#include "GUI/Widgets/DragDropOperation.h"
#include "GUI/Widgets/WidgetTree.h"
#include "RHI/Core/RenderSurfaceContext.h"
#include "RHI/NativeWindow.h"
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
#include <nlohmann/json.hpp>
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

constexpr float kMenuHeight = editor_density::kMenuHeight;

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

EditorSurface::EditorSurface()
    : _dockContext(std::make_shared<FDockContext>())
    , _ownedDockContext(std::make_shared<FDockContext>())
{
}

EditorSurface::~EditorSurface() = default;

void EditorSurface::shutdown()
{
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
    _titleBar.reset();
    _pageTabBar.reset();
    _pageTabKeys.clear();
    _menuBar.reset();
    _workspace.clear();
    _ownedWorkspace.clear();
    _dockFloatingHost.reset();
    _ownedDockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _ownedDockContext.reset();
    _viewportHost = nullptr;
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _projectBrowser.reset();
    _projectSelection.reset();
    _viewportTexture.reset();
    _viewportImageResource.reset();
    _viewportImageView.reset();
    _layer = nullptr;
    _tabSpawners = nullptr;
    _rootSession = nullptr;
    _presentSurface = nullptr;
    _app            = nullptr;
}

void EditorSurface::tick(const FEditorSurfaceContext& context, float dt)
{
    // Called from EditorModule::onPresentation (default window) or extra-window
    // host tick. This is chrome orchestration, not a second product loop.
    if (!_layer || !context.app) {
        return;
    }

    const bool bProjectBrowser = !_layer->isProjectLoaded();
    _presentSurface = context.presentSurface;
    _app            = context.app;
    if (!_tree || _bBuiltAsProjectBrowser != bProjectBrowser) {
        rebuild(context);
    }
    if (!_tree) {
        return;
    }

    applyWindowMetrics(context.metrics);
    _tree->tick(dt);
    pushViewportDisplay();
    UIFrameBuildContext snapshotCtx;
    snapshotCtx.textureResolver = &resolveGameUITexture;
    _snapshot = _tree->buildSnapshot(snapshotCtx);
    publishTitleClientHits();
    publishViewportRect();
    syncViewportHostState(context);
}

void EditorSurface::rebuild(const FEditorSurfaceContext& context)
{
    _presentSurface = context.presentSurface;
    closeViewportContextMenu();
    _bViewportRightPressPending = false;
    _root.reset();
    _titleBar.reset();
    _pageTabBar.reset();
    _pageTabKeys.clear();
    _menuBar.reset();
    _workspace.clear();
    _ownedWorkspace.clear();
    _dockFloatingHost.reset();
    _ownedDockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _ownedDockContext.reset();
    _viewportHost = nullptr;
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _projectBrowser.reset();
    _projectSelection.reset();
    if (_filePicker) {
        _filePicker->reset();
    }
    _filePicker.reset();
    if (_settings) {
        _settings->reset();
    }
    _settings.reset();

    _tree = std::make_unique<WidgetTree>(Extent2D{
        .width  = std::max(context.metrics.logicalExtent.width, 1u),
        .height = std::max(context.metrics.logicalExtent.height, 1u),
    });
    _tree->setDpiScale(context.metrics.dpiScale > 0.0f ? context.metrics.dpiScale : 1.0f);
    _tree->setTextureSource(&gameUITextureSource());
    bindSdlClipboard(*_tree);
    _theme = buildEditorTheme(true);
    _tree->setTheme(_theme.get());

    if (_layer->isProjectLoaded()) {
        _bBuiltAsProjectBrowser = false;
        buildEditorChrome(context);
    }
    else {
        _bBuiltAsProjectBrowser = true;
        buildProjectBrowser(*context.app);
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
    _projectSelection = std::make_shared<SelectionModel>();
    _projectBrowser->roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    auto list = ui::treeView("ProjectList")
                    .bindData(_projectBrowser->roots)
                    .bindSelection(_projectSelection->primaryRef())
                    .setOnSelectionChanged([this](const std::string& id) {
                        _projectSelection->select(id);
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
    (void)ui::attach(*_tree, *_tree->getLayer(WidgetTree::ELayer::Content), std::move(page).release(), ui::canvasSlot().fill());
    refreshProjectBrowserRows();
}

void EditorSurface::buildEditorChrome(const FEditorSurfaceContext& context)
{
    App* app = context.app;
    YA_CORE_ASSERT(_rootSession, "EditorSurface chrome requires the active EditorRootSession");
    YA_CORE_ASSERT(app, "EditorSurface chrome requires App");
    ActionMap&  actions = _rootSession->actions();
    UndoStack&  undo    = _rootSession->undo();
    if (!actions.find("scene.new")) {
        registerEditorActions(actions,
                              *_layer,
                              undo,
                              [this]() { openSceneSaveDialog(); },
                              [this]() { openEditorSettingsDialog(); });
    }
    _root = ui::canvasPanel("EditorRoot").share();
    FCanvasSlotArgs fillArgs;
    fillArgs.anchorMin = {0.0f, 0.0f};
    fillArgs.anchorMax = {1.0f, 1.0f};
    const WidgetAttachment attached = _tree->attachToLayer(WidgetTree::ELayer::Content, _root, fillArgs);
    YA_CORE_ASSERT(attached.valid(), "EditorSurface: failed to attach editor root");
    (void)ui::attach(*_tree,
                     *_root,
                     ui::border("EditorRootFill")
                         .setStyleKey("panel.window")
                         .setVisibility(EWidgetVisibility::HitTestInvisible)
                         .release(),
                     ui::canvasSlot().fill());

    const float titleH   = context.metrics.chromeInsetTop > 0.0f
                             ? context.metrics.chromeInsetTop
                             : kMenuHeight;
    const float menuY    = titleH;
    const float chromeTop = menuY + kMenuHeight;

    _titleBar = std::make_shared<UICanvasPanel>("EditorTitleBar");
    _titleBar->setVisibility(EWidgetVisibility::SelfHitTestInvisible);
    (void)ui::attach(*_tree,
                    *_root,
                    _titleBar,
                    ui::canvasSlot()
                        .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                        .size({0.0f, titleH}));
    (void)ui::attach(*_tree,
                     *_titleBar,
                     ui::border("EditorTitleBarFill")
                         .setStyleKey("panel.titlebar")
                         .setVisibility(EWidgetVisibility::HitTestInvisible)
                         .release(),
                     ui::canvasSlot().fill());

    _pageTabBar = std::make_shared<UITabBar>("EditorPageTabs");
    _pageTabBar->_bDraggableTabs = true;
    _pageTabBar->_emptyPlaceholder = "Pages";
    _pageTabBar->_onTabSelected = [this](int index) {
        if (index < 0 || index >= static_cast<int>(_pageTabKeys.size()) || !_dockContext) {
            return;
        }
        (void)_dockContext->activatePanel(_pageTabKeys[static_cast<size_t>(index)]);
    };
    _pageTabBar->_onTabDragBegin = [this](int index, const std::string&) {
        beginPageTabDrag(index);
    };
    _pageTabBar->_onTabReordered = [this](int from, int to) {
        if (!_dockContext || from < 0 || to < 0 ||
            from >= static_cast<int>(_pageTabKeys.size()) ||
            to >= static_cast<int>(_pageTabKeys.size())) {
            return;
        }
        const FDockContext::FPanel* panel =
            _dockContext->findPanelByStableKey(_pageTabKeys[static_cast<size_t>(from)]);
        const FDockContext::FPanel* dest =
            _dockContext->findPanelByStableKey(_pageTabKeys[static_cast<size_t>(to)]);
        if (!panel || !dest) {
            return;
        }
        const FDockNode* leaf = _dockContext->dockModel().findLeafForPanel(panel->id);
        if (!leaf || _dockContext->dockModel().findLeafForPanel(dest->id) != leaf) {
            return;
        }
        int leafFrom = -1;
        int leafTo   = -1;
        for (int i = 0; i < static_cast<int>(leaf->panelIds.size()); ++i) {
            if (leaf->panelIds[static_cast<size_t>(i)] == panel->id) {
                leafFrom = i;
            }
            if (leaf->panelIds[static_cast<size_t>(i)] == dest->id) {
                leafTo = i;
            }
        }
        if (leafFrom < 0 || leafTo < 0) {
            return;
        }
        const size_t insert = static_cast<size_t>(leafTo > leafFrom ? leafTo + 1 : leafTo);
        if (!_dockContext->dockModel().movePanel(panel->id, leaf->id, insert, false)) {
            return;
        }
        std::string key = _pageTabKeys[static_cast<size_t>(from)];
        _pageTabKeys.erase(_pageTabKeys.begin() + from);
        _pageTabKeys.insert(_pageTabKeys.begin() + to, std::move(key));
        _dockContext->notifyDockLayoutListeners();
    };
    _pageTabBar->_canBeginTabDrag = [this](int index) {
        if (!_pageTabBar || index < 0 || index >= static_cast<int>(_pageTabKeys.size())) {
            return false;
        }
        if (_tabSpawners) {
            if (const FEditorTabSpawner* spawner = _tabSpawners->find(_pageTabKeys[static_cast<size_t>(index)])) {
                return canTearOffEditorTab(spawner->detachPolicy);
            }
        }
        UITabButton* button = _pageTabBar->tabAt(index);
        return button && button->_bDraggable;
    };
    {
        auto drop = std::make_shared<UIDropTargetBehavior>();
        drop->canAccept = [this](UIElement&, const UIDragDropOperation& operation, const glm::vec2& point) {
            return acceptPageTabDrop(operation, point);
        };
        drop->canPreview = drop->canAccept;
        drop->handleDrop = [this](UIElement&, const UIDragDropOperation& operation, const glm::vec2&) {
            dropOntoPageTabs(operation);
        };
        _pageTabBar->addBehavior(drop);
    }
    (void)ui::attach(*_tree,
                    *_root,
                    _pageTabBar,
                    ui::canvasSlot()
                        .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                        .size({0.0f, titleH})
                        .insets(FMargin{context.metrics.chromeInsetLeft,
                                        0.0f,
                                        context.metrics.chromeDragGutter,
                                        0.0f}));

    _menuBar = ui::menuBar("EditorMenu").share();
    (void)ui::attach(*_tree,
                    *_root,
                    _menuBar,
                    ui::canvasSlot()
                        .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                        .offset({0.0f, menuY})
                        .size({0.0f, kMenuHeight}));

    _menuBar->addItem("File", [this]() {
        return UIMenu::create({
            UIMenu::FItem::fromAction(_rootSession->actions(), "scene.new"),
            UIMenu::FItem::fromAction(_rootSession->actions(), "scene.save"),
            UIMenu::FItem::fromAction(_rootSession->actions(), "scene.saveAs"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(_rootSession->actions(), "app.exit"),
        });
    });
    _menuBar->addItem("Edit", [this]() {
        return UIMenu::create({
            UIMenu::FItem::fromAction(_rootSession->actions(), "edit.undo"),
            UIMenu::FItem::fromAction(_rootSession->actions(), "edit.redo"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(_rootSession->actions(), "selection.duplicate"),
            UIMenu::FItem::fromAction(_rootSession->actions(), "selection.delete"),
        });
    });
    _menuBar->addItem("View", [this]() {
        std::vector<UIMenu::FItem> items = {
            UIMenu::FItem::fromAction(_rootSession->actions(), "viewport.mode3d"),
            UIMenu::FItem::fromAction(_rootSession->actions(), "viewport.mode2d"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(_rootSession->actions(), "editor.settings"),
        };
        // Generated editor companions (camera body, light icons) are editor
        // furniture, so the authoring viewport draws them while authoring and
        // on request afterwards. The option belongs to the editor, not the app.
        {
            UIMenu::FItem gizmoItem;
            gizmoItem.label      = "Show Editor Gizmos";
            gizmoItem.bCheckable = true;
            gizmoItem.bChecked   = _layer && _layer->isEditorGizmoShown();
            gizmoItem.action     = [this]() {
                if (_layer) {
                    _layer->setEditorGizmoShown(!_layer->isEditorGizmoShown());
                }
            };
            items.push_back(UIMenu::FItem::separator());
            items.push_back(std::move(gizmoItem));
        }
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

    _dockContext = std::make_shared<FDockContext>();
    _dockContext->bAllowFloating = true;
    _dockContext->bAllowTearOff  = true;
    _dockContext->sourceScope    = EDockSourceScope::WindowRoot;
    _dockContext->hostWindowId   = _windowId;
    _ownedDockContext = std::make_shared<FDockContext>();
    _ownedDockContext->bAllowFloating = true;
    _ownedDockContext->bAllowTearOff  = true;
    _ownedDockContext->sourceScope    = EDockSourceScope::EditorOwned;
    _ownedDockContext->hostWindowId   = _windowId;
    _dockSpace = ui::dockSpace("EditorDock").setContext(_dockContext).share();
    (void)ui::attach(*_tree,
                    *_root,
                    _dockSpace,
                    ui::canvasSlot()
                        .fill()
                        .insets(FMargin{editor_density::kChromeInset,
                                        chromeTop,
                                        editor_density::kChromeInset,
                                        editor_density::kChromeInset}));

    FCanvasSlotArgs floatingFill;
    floatingFill.anchorMin = {0.0f, 0.0f};
    floatingFill.anchorMax = {1.0f, 1.0f};
    _dockFloatingHost = std::make_shared<UIDockFloatingHost>("EditorDockFloatingHost");
    _dockFloatingHost->bindContext(_dockContext);
    (void)_tree->attachToLayer(WidgetTree::ELayer::Popup, _dockFloatingHost, floatingFill);
    _ownedDockFloatingHost = std::make_shared<UIDockFloatingHost>("EditorOwnedDockFloatingHost");
    _ownedDockFloatingHost->bindContext(_ownedDockContext);
    (void)_tree->attachToLayer(WidgetTree::ELayer::Popup, _ownedDockFloatingHost, floatingFill);

    auto persistAll = [this]() { persistDockLayouts(); };
    auto rootFor = [this](EditorRootId id) -> EditorRootSession* { return _roots.find(id); };
    _ownedWorkspace.bind(EditorDockWorkspace::FHost{
        .tree            = _tree.get(),
        .layer           = _layer,
        .selection       = &_rootSession->selection(),
        .actions         = &_rootSession->actions(),
        .undo            = &_rootSession->undo(),
        .viewportHost    = this,
        .spawners        = _tabSpawners,
        .documents       = _documents,
        .dock            = _ownedDockContext.get(),
        .activeRootId    = _rootSession->id(),
        .targetPlacement = EEditorTabPlacement::EditorOwnedNested,
        .windowId        = _windowId,
        .documentKey     = _layer->getCurrentScenePath(),
        .app             = context.app,
        .presentSurface  = context.presentSurface,
        .persistAll      = persistAll,
        .rootFor         = rootFor,
    });
    _workspace.bind(EditorDockWorkspace::FHost{
        .tree            = _tree.get(),
        .layer           = _layer,
        .selection       = &_rootSession->selection(),
        .actions         = &_rootSession->actions(),
        .undo            = &_rootSession->undo(),
        .viewportHost    = this,
        .spawners        = _tabSpawners,
        .documents       = _documents,
        .dock            = _dockContext.get(),
        .menuBar         = _menuBar.get(),
        .activeRootId    = _rootSession->id(),
        .targetPlacement = EEditorTabPlacement::WindowRootDock,
        .windowId        = _windowId,
        .documentKey     = _layer->getCurrentScenePath(),
        .nestedWorkspace = &_ownedWorkspace,
        .nestedDock      = _ownedDockContext,
        .app             = context.app,
        .presentSurface  = context.presentSurface,
        .persistAll      = persistAll,
        .rootFor         = rootFor,
    });
    _workspace.buildWindowMenu();
    _workspace.applyWorkspaceLayout();
    _dockContext->fireDockUpdated();
    _ownedDockContext->fireDockUpdated();
    _dockContext->appendOnDockUpdated([this]() { syncPageTabs(); });
    _dockContext->appendOnDockUpdated(persistAll);
    _dockContext->appendOnFloatingUpdated(persistAll);
    _ownedDockContext->appendOnDockUpdated(persistAll);
    _ownedDockContext->appendOnFloatingUpdated(persistAll);
    installDockNoTargetTearOff();
    installEmptyTabBarZoom();
    syncPageTabs();

    if (const std::optional<std::string>& editorTab = app->getDesc().editorTab; editorTab && !editorTab->empty()) {
        _workspace.invokeTab(*editorTab);
    }

    _viewportGizmoOverlay = std::make_shared<EditorViewportGizmoOverlay>(_layer->gizmo());
    _viewportOverlayHost.setOverlay(_viewportGizmoOverlay);
    _layer->gizmo().setUndoStack(&_rootSession->undo());
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

void EditorSurface::persistDockLayouts()
{
    if (_persistLayout) {
        _persistLayout();
        return;
    }
    if (!_dockContext || !_ownedDockContext) {
        return;
    }
    nlohmann::json window;
    window["role"]        = "main";
    window["windowId"]    = _windowId;
    window["bounds"]      = nlohmann::json::array({0, 0, 0, 0});
    window["hasOrigin"]   = false;
    window["monitor"]     = -1;
    window["maximized"]   = false;
    window["windowRoot"]  = _dockContext->exportLayoutJson();
    window["ownedNested"] = _ownedDockContext->exportLayoutJson();
    nlohmann::json document;
    document["version"] = 4;
    document["windows"] = nlohmann::json::array({std::move(window)});
    ConfigManager::Editor("editor").set("dockLayout", document).flush();
}

void EditorSurface::setOnDockNoTargetTearOff(
    std::function<bool(FDockContext&, uint64_t, const glm::vec2&, const glm::vec2&)> fn)
{
    _onDockNoTargetTearOff = std::move(fn);
    installDockNoTargetTearOff();
}

void EditorSurface::installDockNoTargetTearOff()
{
    auto bind = [this](const std::shared_ptr<FDockContext>& dock) {
        if (!dock) {
            return;
        }
        if (!_onDockNoTargetTearOff) {
            dock->realizeNoTargetTearOff = nullptr;
            return;
        }
        FDockContext* raw = dock.get();
        dock->realizeNoTargetTearOff =
            [this, raw](DockPanelId id, const glm::vec2& pos, const glm::vec2& size) {
                return _onDockNoTargetTearOff(*raw, id, pos, size);
            };
    };
    bind(_dockContext);
    bind(_ownedDockContext);
}

void EditorSurface::installEmptyTabBarZoom()
{
    auto toggle = [this]() {
        if (!_presentSurface) {
            return;
        }
        INativeWindow* native = _presentSurface->getNativeWindow();
        if (!native) {
            return;
        }
        (void)toggleWindowChromeTitleZoom(*native);
    };
    if (_pageTabBar) {
        _pageTabBar->_onStripDoubleClick = toggle;
    }
}

void EditorSurface::publishTitleClientHits()
{
    if (!_presentSurface) {
        return;
    }
    INativeWindow* native = _presentSurface->getNativeWindow();
    if (!native) {
        return;
    }
    std::vector<FWindowChromeRect> hits;
    if (_pageTabBar) {
        const Rect2D& rect = _pageTabBar->getLayoutRect();
        if (rect.extent.x > 0.0f && rect.extent.y > 0.0f) {
            hits.push_back(FWindowChromeRect{
                rect.pos.x,
                rect.pos.y,
                rect.extent.x,
                rect.extent.y,
            });
        }
        YA_CORE_ASSERT(rect.extent.x <= 0.0f || rect.extent.y <= 0.0f || !hits.empty(),
                       "EditorSurface: page tab bar has a layout rect but no title Client hit was published");
    }
    updateWindowChromeTitleClientHits(*native, hits);
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
    items.push_back(UIMenu::FItem::fromAction(_rootSession->actions(), "selection.createEmpty"));
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
    items.push_back(UIMenu::FItem::fromAction(_rootSession->actions(), "selection.duplicate"));
    items.push_back(UIMenu::FItem::fromAction(_rootSession->actions(), "selection.delete"));

    auto menu = UIMenu::create(std::move(items));
    _viewportContextMenu = menu;
    menu->_onDismiss = [this]() { _viewportContextMenu.reset(); };
    menu->openAt(*_tree, windowPoint);
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

void EditorSurface::pushViewportDisplay()
{
    if (!_viewportHost || !_layer) {
        return;
    }
    const auto& display = _layer->getViewportDisplayImage();
    const bool expectsViewport = _layer->getHierarchyScene() != nullptr;

    auto sourceImage     = display ? display->getImageShared() : nullptr;
    auto sourceImageView = display ? display->getImageViewShared() : nullptr;
    if (!display || !display->isValid() || !sourceImage || !sourceImageView) {
        _viewportHost->setDisplayImage(nullptr, expectsViewport);
        _viewportTexture.reset();
        _viewportImageResource.reset();
        _viewportImageView.reset();
    }
    else if (_viewportTexture &&
             _viewportImageResource == sourceImage &&
             _viewportImageView == sourceImageView) {
        _viewportHost->setDisplayImage(_viewportTexture, false);
    }
    else {
        _viewportImageResource = std::move(sourceImage);
        _viewportImageView     = std::move(sourceImageView);
        _viewportTexture       = Texture::wrap(_viewportImageResource,
                                         _viewportImageView,
                                         "EditorSurfaceViewport");
        _viewportHost->setDisplayImage(_viewportTexture, false);
    }

    // Chrome stacked on the world image is pushed with it: same frame, same
    // origin, same coordinate space (viewport-local logical pixels). A cleared
    // texture collapses the panel, which is also what stops the layer from
    // excluding that area from world input.
    _viewportHost->setPreviewImage(_layer->getViewportPreviewImage(), previewPanelLocalRect());
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

bool EditorSurface::invokeTab(std::string_view tabId)
{
    return _workspace.invokeTab(tabId);
}

bool EditorSurface::openDocumentEditor(EEditorDocumentKind kind, std::string key)
{
    if (key.empty() || !_documents) {
        return false;
    }
    EditorRootId rootId = kInvalidEditorRootId;
    switch (kind) {
    case EEditorDocumentKind::UI: {
        rootId = kUIEditorRootId;
        break;
    }
    case EEditorDocumentKind::Material: {
        rootId = kMaterialEditorRootId;
        break;
    }
    case EEditorDocumentKind::Script: {
        rootId = kScriptEditorRootId;
        break;
    }
    default: {
        return false;
    }
    }
    EditorRootSession* root = _roots.find(rootId);
    if (!root) {
        return false;
    }
    EditorDocumentSession* session = _documents->open({kind, key}, EEditorDocumentClosePolicy::RejectIfDirty);
    if (!session) {
        return false;
    }
    root->bindDocument(session);
    (void)_documents->claimPreview(session->id());
    const char* tabId = editorRootTabId(rootId);
    return tabId && invokeTab(tabId);
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
    App* app = _app;
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
        .fontOptions = []() { return ui_font_settings::availableFaces(); },
        .fontFace = []() { return ui_font_settings::faceId(); },
        .setFontFace = [app](std::string faceId) {
            // Swap the stack here; no tree invalidation is needed. The reload
            // bumps FontManager's atlas revision, and WidgetTree polls it at the
            // start of the NEXT snapshot, so every label re-measures against the
            // new metrics on the coming frame without a bespoke dirty hook.
            IRender* render = app ? app->getRenderServices().getRender() : nullptr;
            if (render) {
                ui_font_settings::applyAndStore(*render, faceId);
            }
        },
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
    // Panel geometry, both rects of it: the world image and the camera preview
    // panel stacked on it. The layer needs the second one because chrome sits
    // over the world image, so a point on the panel must not map to a world
    // point (see EditorLayer::screenToViewport).
    const Rect2D previewLocal = previewPanelLocalRect();
    _layer->notifyViewportWidgetRect(
        rect,
        Rect2D{
            .pos    = rect.pos + previewLocal.pos,
            .extent = previewLocal.extent,
        });
    const bool hovered = _viewportHost->isHovered();
    const bool focused = _viewportHost->isFocused() || hovered;
    _layer->setViewportHoverFocus(hovered, focused);
}

Rect2D EditorSurface::previewPanelLocalRect() const
{
    if (!_layer || !_layer->getViewportPreviewImage() || !_viewportHost) {
        return {};
    }
    // Viewport-local, computed from the same rect the panel is placed with, so
    // the chrome and the layer's input exclusion never disagree.
    return EditorViewProducer::previewRect(_viewportHost->imageRect());
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

    if (event.getEventType() == EEvent::MouseButtonPressed && _presentSurface) {
        const auto& press = static_cast<const MouseButtonPressedEvent&>(event);
        if (press.GetMouseButton() == EMouse::Left && press.clickCount() >= 2) {
            if (INativeWindow* native = _presentSurface->getNativeWindow()) {
                if (handleWindowChromeTitleDoubleClick(*native, windowPoint.x, windowPoint.y)) {
                    return EWidgetRouteResult::HandledExclusive;
                }
            }
        }
    }

    bool bPopupOpen = false;
    if (UIElement* popup = _tree->getLayer(WidgetTree::ELayer::Popup)) {
        bPopupOpen = !popup->getChildren().empty();
    }
    if (handleGuiFrameInspectorHudInput(event, windowPoint, _tree->getLogicalExtent(), bPopupOpen)) {
        return EWidgetRouteResult::HandledExclusive;
    }

    constexpr float kViewportContextDragSlop = 4.0f;
    // World interaction only on the world image. Chrome stacked over it (the
    // camera preview panel) answers through the GUI router instead, so a gizmo
    // handle drawn underneath the panel does not steal a click on the panel.
    const bool bOnWorldImage = isPointInViewport(windowPoint) && _viewportHost->isWorldPoint(windowPoint);
    const bool overlayCandidate =
        _viewportHost &&
        (bOnWorldImage || _viewportOverlayHost.wantsPointerCapture() ||
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
        if (canAuthor && bOnWorldImage) {
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
            _bViewportRightPressPending && canAuthor && bOnWorldImage;
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
    if (event.getEventType() == EEvent::KeyPressed && _rootSession &&
        _rootSession->actions().dispatchKey(static_cast<const KeyPressedEvent&>(event), wantsTextInput())) {
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
    return _tree && _tree->wantsTextInput();
}

void EditorSurface::syncPageTabs()
{
    if (!_pageTabBar || !_dockContext) {
        return;
    }

    std::vector<std::string> keys;
    std::vector<std::string> titles;
    std::vector<bool> closable;
    int selected = -1;
    DockNodeId pageLeaf = _dockContext->dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    if (pageLeaf == kInvalidDockNodeId) {
        for (const DockNodeId id : _dockContext->dockModel().leafIds()) {
            if (const FDockNode* leaf = _dockContext->dockModel().findNode(id);
                leaf && leaf->bHideTabBar) {
                pageLeaf = id;
                break;
            }
        }
    }
    const FDockNode* leaf = pageLeaf != kInvalidDockNodeId ? _dockContext->dockModel().findNode(pageLeaf)
                                                           : nullptr;
    if (leaf && leaf->kind == EDockNodeKind::Stack) {
        if (!leaf->bHideTabBar) {
            (void)_dockContext->dockModel().setHideTabBar(leaf->id, true);
        }
        if (_dockSpace) {
            _dockSpace->syncTabBarVisibility();
        }
        for (const DockPanelId panelId : leaf->panelIds) {
            const FDockPanelRecord* record = _dockContext->dockModel().findPanel(panelId);
            if (!record) {
                continue;
            }
            if (_tabSpawners) {
                const FEditorTabSpawner* spawner = _tabSpawners->find(record->stableKey);
                if (spawner && spawner->scope != EEditorTabScope::WindowRootEditor) {
                    continue;
                }
            }
            if (panelId == leaf->selectedPanel) {
                selected = static_cast<int>(keys.size());
            }
            keys.push_back(record->stableKey);
            titles.push_back(record->title);
            closable.push_back(record->closable);
        }
    }

    std::vector<bool> draggable(keys.size(), true);
    if (_tabSpawners) {
        for (size_t i = 0; i < keys.size(); ++i) {
            const FEditorTabSpawner* spawner = _tabSpawners->find(keys[i]);
            if (spawner) {
                draggable[i] = canTearOffEditorTab(spawner->detachPolicy);
            }
        }
    }

    if (keys != _pageTabKeys || _pageTabBar->tabCount() != static_cast<int>(keys.size())) {
        _pageTabBar->clearTabs();
        _pageTabKeys = keys;
        for (size_t i = 0; i < keys.size(); ++i) {
            UITabButton* button = _pageTabBar->addTab(titles[i]);
            button->_bClosable  = closable[i];
            button->_bDraggable = draggable[i];
            if (closable[i]) {
                const std::string key = keys[i];
                button->_onClose = [this, key]() {
                    if (_dockContext) {
                        (void)_dockContext->closePanel(key);
                    }
                };
            }
        }
    }
    for (int i = 0; i < _pageTabBar->tabCount() && i < static_cast<int>(draggable.size()); ++i) {
        if (UITabButton* button = _pageTabBar->tabAt(i)) {
            button->_bDraggable = draggable[static_cast<size_t>(i)];
        }
    }
    if (selected >= 0) {
        _pageTabBar->syncSelectedTab(selected);
    }
}

void EditorSurface::beginPageTabDrag(int index)
{
    if (!_tree || !_dockContext || index < 0 || index >= static_cast<int>(_pageTabKeys.size())) {
        return;
    }
    UITabButton* button = _pageTabBar ? _pageTabBar->tabAt(index) : nullptr;
    if (!button || !_pageTabBar->allowsArmedTabDrag(*button)) {
        return;
    }
    if (_tabSpawners) {
        if (const FEditorTabSpawner* spawner = _tabSpawners->find(_pageTabKeys[static_cast<size_t>(index)]);
            spawner && !canTearOffEditorTab(spawner->detachPolicy)) {
            return;
        }
    }
    const FDockContext::FPanel* panel = _dockContext->findPanelByStableKey(_pageTabKeys[static_cast<size_t>(index)]);
    const FDockPanelRecord* record = panel ? _dockContext->dockModel().findPanel(panel->id) : nullptr;
    if (!panel || !record) {
        return;
    }
    const DockPanelId panelId = panel->id;
    std::string label = record->title;
    auto operation = FDockPanelDragDropOp::make(panelId, std::move(label), _dockContext.get());
    if (canCloseEditorWindow(_windowId) && _dockContext->panelStableKeys().size() == 1) {
        operation->bHideSourceWindowOnLeave = true;
    }
    DragSessionObserver observer;
    observer.onFinished = [this, panelId](EDragFinishResult result, const glm::vec2& logicalPoint, std::string_view) {
        if (result == EDragFinishResult::Dropped || result == EDragFinishResult::Cancelled) {
            return;
        }
        if (logicalPoint.x < -10000.0f || logicalPoint.y < -10000.0f) {
            return;
        }
        if (result == EDragFinishResult::NoTarget && _dockContext && _dockContext->bAllowTearOff) {
            const glm::vec2 size{320.0f, 240.0f};
            bool bHandled = false;
            if (_dockContext->realizeNoTargetTearOff) {
                bHandled = _dockContext->realizeNoTargetTearOff(panelId, logicalPoint, size);
            }
            if (!bHandled) {
                _dockContext->tearOffPanel(panelId, logicalPoint, size);
            }
            _dockContext->fireFloatingUpdated();
            _dockContext->notifyDockLayoutListeners();
        }
    };
    _tree->beginDrag(_pageTabBar.get(), std::move(operation), std::move(observer));
}

bool EditorSurface::acceptPageTabDrop(const UIDragDropOperation& operation, const glm::vec2&)
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || !_dockContext || dockOp->panelId == kInvalidDockPanelId) {
        return false;
    }
    const FDockContext* source = dockOp->sourceContext ? dockOp->sourceContext : _dockContext.get();
    const FDockPanelRecord* record = source->dockModel().findPanel(dockOp->panelId);
    const FDockContext::FPanel* panel = source->findPanel(dockOp->panelId);
    if (!record || !panel) {
        return false;
    }
    if (_tabSpawners) {
        const FEditorTabSpawner* spawner = _tabSpawners->find(record->stableKey);
        if (!spawner || spawner->scope != EEditorTabScope::WindowRootEditor) {
            return false;
        }
        FEditorTabDragPayload payload;
        payload.tabId = spawner->tabId;
        payload.scope = spawner->scope;
        payload.ownerEditorId = panel->ownerEditorId != 0
                                    ? static_cast<EditorRootId>(panel->ownerEditorId)
                                    : spawner->ownerEditorId;
        payload.detachPolicy = spawner->detachPolicy;
        if (!canAcceptEditorDrop(payload, EEditorTabPlacement::WindowPageTab, kInvalidEditorRootId)) {
            return false;
        }
    }
    return true;
}

void EditorSurface::dropOntoPageTabs(const UIDragDropOperation& operation)
{
    const auto* dockOp = operation.as<FDockPanelDragDropOp>();
    if (!dockOp || !_dockContext) {
        return;
    }
    DockPanelId panelId = dockOp->panelId;
    const bool bImport = dockOp->sourceContext && dockOp->sourceContext != _dockContext.get();
    if (bImport) {
        std::optional<FDockContext::FDockExtractedPanel> extracted =
            dockOp->sourceContext->extractPanel(panelId);
        if (!extracted) {
            return;
        }
        panelId = _dockContext->adoptPanel(std::move(*extracted));
        if (panelId == kInvalidDockPanelId) {
            return;
        }
    }
    DockNodeId pageLeaf = _dockContext->dockModel().findFirstLeafWithRole(EDockLeafRole::Page);
    if (pageLeaf == kInvalidDockNodeId) {
        return;
    }
    if (_dockContext->dockModel().findLeafForPanel(panelId)) {
        (void)_dockContext->dockModel().movePanel(panelId, pageLeaf);
    }
    else {
        (void)_dockContext->dockModel().addPanel(panelId, pageLeaf);
    }
    (void)_dockContext->dockModel().setHideTabBar(pageLeaf, true);
    if (const FDockPanelRecord* record = _dockContext->dockModel().findPanel(panelId)) {
        (void)_dockContext->activatePanel(record->stableKey);
    }
    else {
        (void)_dockContext->dockModel().selectPanel(panelId);
        _dockContext->fireDockUpdated();
        return;
    }
    _dockContext->fireDockUpdated();
}

} // namespace ya
