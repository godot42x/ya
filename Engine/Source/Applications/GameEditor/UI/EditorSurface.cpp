#include "GameEditor/UI/EditorSurface.h"
#include "GameEditor/UI/EditorAssetPicker.h"
#include "GameEditor/UI/EditorFilePicker.h"
#include "GameEditor/UI/EditorFilePickerDialog.h"
#include "GameEditor/UI/EditorSettingsDialog.h"
#include "GameEditor/UI/EditorInspectorTab.h"

#include "Core/Event.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "GameEditor/UI/EditorViewportHost.h"
#include "GameEditor/UI/EditorHierarchyOps.h"
#include "GameEditor/UI/EditorViewportGizmoOverlay.h"
#include "GameEditor/UI/EditorListRows.h"
#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "ECS/ECSRegistry.h"
#include "GUI/Declarative/Build.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GameEditor/Services/NodeCreateRegistry.h"
#include "GameRuntime/App.h"
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
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/WidgetAttachment.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/System/PathUtils.h"
#include "Hierarchy/Node.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Render.h"
#include "Render/Resources/FontManager.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace ya
{

namespace
{

constexpr float kMenuHeight     = editor_density::kMenuHeight;
constexpr float kToolbarHeight  = editor_density::kToolbarHeight;
constexpr float kChromeTop      = kMenuHeight + kToolbarHeight;

bool parseWidgetEntryKey(const std::string& id, std::string& outEntryId)
{
    constexpr std::string_view kPrefix = "ui:";
    if (id.size() <= kPrefix.size() || id.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    outEntryId = id.substr(kPrefix.size());
    return true;
}

UITreeView::FNode buildHierarchyNode(Node* node)
{
    UITreeView::FNode out;
    if (!node) {
        return out;
    }
    uint64_t uuid = 0;
    if (Entity* entity = node->getEntity()) {
        if (auto* id = entity->getComponent<IDComponent>()) {
            uuid = id->_id.value;
        }
    }
    out.id    = editorHierarchyEntityIdKey(uuid);
    out.label = node->getName();
    out.children.reserve(node->getChildCount());
    for (Node* child : node->getChildren()) {
        out.children.push_back(buildHierarchyNode(child));
    }
    return out;
}

void appendHierarchyFingerprint(std::string& fingerprint, const Node* node)
{
    if (!node) {
        return;
    }
    fingerprint += node->getName();
    fingerprint += ':';
    fingerprint += std::to_string(node->getChildCount());
    fingerprint += ';';
    for (const Node* child : node->getChildren()) {
        appendHierarchyFingerprint(fingerprint, child);
    }
}

bool widgetOrAncestor(const UIElement* node, const UIElement* target)
{
    for (const UIElement* cursor = node; cursor; cursor = cursor->getParent()) {
        if (cursor == target) {
            return true;
        }
    }
    return false;
}

DockPanelId dockPanelIdForKey(const FDockTreeModel& model, const char* stableKey)
{
    if (const FDockPanelRecord* record = model.findPanelByStableKey(stableKey)) {
        return record->id;
    }
    return kInvalidDockPanelId;
}

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

constexpr std::string_view kDefaultWorkspaceTabs[] = {
    "viewport",
    "hierarchy",
    "inspector",
    "content-browser",
    "frame-stats",
    "gui-workbench",
    "runtime-tools",
    "ui-designer",
    "asset-inspector",
    "debug-images",
};

} // namespace

EditorSurface::EditorSurface() = default;

EditorSurface::~EditorSurface() = default;

void EditorSurface::shutdown()
{
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
    _dockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _viewportImage.reset();
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _hierarchyView.reset();
    _hierarchyRoots.reset();
    _hierarchyFilter.reset();
    _hierarchyFilterField.reset();
    _selection = std::make_shared<SelectionModel>();
    _actions   = std::make_shared<ActionMap>();
    _undo      = std::make_shared<UndoStack>();
    _projectErrorText.reset();
    _viewportTexture.reset();
    _viewportImageResource.reset();
    _viewportImageView.reset();
    _hierarchyFingerprint.clear();
    _syncedSelectionGeneration = ~uint64_t{0};
    _layer = nullptr;
    _tabSpawners = nullptr;
}

void EditorSurface::tick(App& app, float dt)
{
    if (!_layer) {
        return;
    }

    const bool bProjectLoaded = _layer->isProjectLoaded();
    if (!_tree || _bBuiltAsProjectBrowser == bProjectLoaded) {
        rebuild(app);
    }

    applyWindowMetrics(app);
    _tree->tick(dt);
    syncPresentation(app, dt);
    _snapshot = _tree->buildSnapshot(UIFrameBuildContext{});
    publishViewportRect();
    syncViewportHostState(app);
}

void EditorSurface::rebuild(App& app)
{
    _root.reset();
    _menuBar.reset();
    _toolbarModeText.reset();
    _dockFloatingHost.reset();
    _dockSpace.reset();
    _dockContext.reset();
    _viewportImage.reset();
    _viewportGizmoOverlay.reset();
    _viewportOverlayHost.clearOverlay();
    _hierarchyView.reset();
    _hierarchyRoots.reset();
    _hierarchyFilter.reset();
    _hierarchyFilterField.reset();
    _selection = std::make_shared<SelectionModel>();
    _actions   = std::make_shared<ActionMap>();
    _undo      = std::make_shared<UndoStack>();
    _projectErrorText.reset();
    if (_filePicker) {
        _filePicker->reset();
    }
    _filePicker.reset();
    if (_settings) {
        _settings->reset();
    }
    _settings.reset();
    _hierarchyFingerprint.clear();
    _syncedSelectionGeneration = ~uint64_t{0};

    int windowW = 0;
    int windowH = 0;
    if (auto* render = app.getRenderServices().getRender()) {
        render->getWindowSize(windowW, windowH);
    }
    _tree = std::make_unique<WidgetTree>(Extent2D{
        .width  = static_cast<uint32_t>(std::max(windowW, 1)),
        .height = static_cast<uint32_t>(std::max(windowH, 1)),
    });
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
                       .setOnClick([this]() { _layer->requestRefreshProjectBrowser(); });
    auto exitBtn = labeledButton("ExitEditor", "Exit Editor")
                       .setOnClick([]() {
                           if (auto* app = App::get()) {
                               app->requestQuit();
                           }
                       });

    _hierarchyRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    auto list = ui::treeView("ProjectList")
                    .bindData(_hierarchyRoots)
                    .bindSelection(_selection->primaryRef())
                    .setOnSelectionChanged([this](const std::string& id) {
                        _selection->select(id);
                        int index = 0;
                        if (auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), index);
                            ec == std::errc{} && ptr == id.data() + id.size()) {
                            _layer->setProjectBrowserSelection(index);
                        }
                    });
    _hierarchyView = list.share();

    auto openBtn = labeledButton("OpenProject", "Open Project")
                       .setOnClick([this]() {
                           const auto& projects = _layer->getDiscoveredProjects();
                           const int   index    = _layer->getProjectBrowserSelection();
                           if (index >= 0 && index < static_cast<int>(projects.size())) {
                               _layer->requestOpenProject(projects[static_cast<size_t>(index)]);
                           }
                       });

    auto errorText = ui::text("ProjectError").setStyleKey("text.error");
    _projectErrorText = errorText.share();

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
}

void EditorSurface::registerEditorActions()
{
    auto define = [this](FAction action) {
        if (!_actions->define(std::move(action))) {
            YA_CORE_ERROR("EditorSurface: failed to define action");
        }
    };

    define({
        .id      = "scene.new",
        .label   = "New Scene",
        .chord   = FActionChord::primary(EKey::K_N),
        .execute = [this]() { _layer->cmdNewScene(); },
    });
    define({
        .id      = "scene.save",
        .label   = "Save Scene",
        .chord   = FActionChord::primary(EKey::K_S),
        .execute = [this]() { _layer->cmdSaveScene(); },
    });
    define({
        .id      = "scene.saveAs",
        .label   = "Save Scene As",
        .chord   = FActionChord::primary(EKey::K_S, true),
        .execute = [this]() { openSceneSaveDialog(); },
    });
    define({
        .id         = "edit.undo",
        .label      = "Undo",
        .chord      = FActionChord::primary(EKey::K_Z),
        .execute    = [this]() { (void)_undo->undo(); },
        .canExecute = [this]() { return _undo->canUndo(); },
    });
    const FActionChord redoChord =
#if defined(__APPLE__)
        FActionChord::primary(EKey::K_Z, true);
#else
        FActionChord::primary(EKey::K_Y);
#endif
    define({
        .id         = "edit.redo",
        .label      = "Redo",
        .chord      = redoChord,
        .execute    = [this]() { (void)_undo->redo(); },
        .canExecute = [this]() { return _undo->canRedo(); },
    });
    define({
        .id         = "selection.createEmpty",
        .label      = "Create Empty Node",
        .execute    = [this]() { _layer->cmdCreateEmptyNode(); },
        .canExecute = [this]() { return _layer && _layer->canViewportAuthor(); },
    });
    define({
        .id         = "selection.duplicate",
        .label      = "Duplicate",
        .chord      = FActionChord::primary(EKey::K_D),
        .execute    = [this]() { _layer->cmdDuplicateSelection(); },
        .canExecute = [this]() {
            return _layer && _layer->canViewportAuthor() && !_layer->getSelections().empty();
        },
    });
    define({
        .id         = "selection.delete",
        .label      = "Delete",
        .chord      = {.key = EKey::Delete},
        .execute    = [this]() { _layer->cmdDeleteSelection(); },
        .canExecute = [this]() {
            return _layer && _layer->canViewportAuthor() && !_layer->getSelections().empty();
        },
    });
    define({
        .id      = "app.exit",
        .label   = "Exit",
        .execute = []() {
            if (auto* app = App::get()) {
                app->requestQuit();
            }
        },
    });
    define({
        .id      = "editor.settings",
        .label   = "Editor Settings...",
        .execute = [this]() { openEditorSettingsDialog(); },
    });
    define({
        .id      = "viewport.mode3d",
        .label   = "Viewport 3D",
        .execute = [this]() { _layer->setViewportMode(EViewportMode::Mode3D); },
    });
    define({
        .id      = "viewport.mode2d",
        .label   = "Viewport 2D",
        .execute = [this]() { _layer->setViewportMode(EViewportMode::Mode2D); },
    });
    define({
        .id      = "runtime.play",
        .label   = "Play",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() { app->startRuntime(); });
            }
        },
    });
    define({
        .id      = "runtime.simulate",
        .label   = "Simulate",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() { app->startSimulation(); });
            }
        },
    });
    define({
        .id      = "runtime.stop",
        .label   = "Stop",
        .execute = []() {
            if (auto* app = App::get()) {
                app->getTaskManager().registerFrameTask([app]() {
                    if (app->isRuntimeMode()) {
                        app->stopRuntime();
                    }
                    else if (app->isSimulationMode()) {
                        app->stopSimulation();
                    }
                });
            }
        },
    });
}

void EditorSurface::buildEditorChrome(App& app)
{
    (void)app;
    registerEditorActions();
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
        return UIMenu::create({
            UIMenu::FItem::fromAction(*_actions, "viewport.mode3d"),
            UIMenu::FItem::fromAction(*_actions, "viewport.mode2d"),
            UIMenu::FItem::separator(),
            UIMenu::FItem::fromAction(*_actions, "editor.settings"),
        });
    });
    buildToolsMenu();

    auto play = labeledButton("Play", "Play").setOnClick([this]() {
        (void)_actions->execute("runtime.play");
    });
    auto simulate = labeledButton("Simulate", "Simulate").setOnClick([this]() {
        (void)_actions->execute("runtime.simulate");
    });
    auto stop = labeledButton("Stop", "Stop").setOnClick([this]() {
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
                                          ui::canvasSlot().anchor({0.0f, 0.0f}, {1.0f, 1.0f}).offset({0.0f, kChromeTop}));

    _dockFloatingHost = std::make_shared<UIDockFloatingHost>("EditorDockFloatingHost");
    _dockFloatingHost->bindContext(_dockContext);
    FCanvasSlotArgs floatingFill;
    floatingFill.anchorMin = {0.0f, 0.0f};
    floatingFill.anchorMax = {1.0f, 1.0f};
    (void)_tree->attachToLayer(WidgetTree::ELayer::Popup, _dockFloatingHost, floatingFill);

    materializeWorkspaceTabs();
    _dockContext->setPanelClosable("viewport", false);
    _dockContext->setPanelClosable("hierarchy", false);
    _dockContext->setPanelClosable("inspector", false);
    if (!tryRestoreEditorDockLayout()) {
        applyDefaultEditorDockLayout();
    }
    _dockContext->fireDockUpdated();
    _dockContext->appendOnDockUpdated([this]() { persistEditorDockLayout(); });
    _dockContext->appendOnFloatingUpdated([this]() { persistEditorDockLayout(); });

    if (const std::optional<std::string>& editorTab = app.getDesc().editorTab; editorTab && !editorTab->empty()) {
        invokeTab(*editorTab);
    }

    _viewportGizmoOverlay = std::make_shared<EditorViewportGizmoOverlay>(*_layer);
    _viewportOverlayHost.setOverlay(_viewportGizmoOverlay);
    _layer->setViewportGizmoUndoStack(_undo.get());
}

FEditorTabSpawnContext EditorSurface::makeSpawnContext()
{
    return FEditorTabSpawnContext{
        .tree = *_tree,
        .layer = *_layer,
        .selection = *_selection,
        .actions = *_actions,
        .undo = *_undo,
        .authoringParent = _root.get(),
    };
}

std::shared_ptr<UIElement> EditorSurface::buildViewportBody()
{
    auto viewportImage = ui::image("ViewportImage");
    _viewportImage     = viewportImage.share();
    _viewportImage->_hitFilter = EWidgetHitFilter::Stop;
    return ui::panel("ViewportBody")
        .setStyleKey("panel.canvas")
        .child(std::move(viewportImage), ui::canvasSlot().fill())
        .release();
}

std::shared_ptr<UIElement> EditorSurface::buildHierarchyBody()
{
    _hierarchyFilter = std::make_shared<Reactive<std::string>>("");
    auto hierarchyFilterField = ui::textField("HierarchyFilter")
                                    .setOnTextChanged([this](const std::string& text) {
                                        if (_hierarchyFilter) {
                                            _hierarchyFilter->set(text);
                                        }
                                    });
    _hierarchyFilterField = hierarchyFilterField.share();

    _hierarchyRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _hierarchyView  = ui::treeView("HierarchyTree")
                         .bindData(_hierarchyRoots)
                         .bindFilter(_hierarchyFilter)
                         .bindSelection(_selection->primaryRef())
                         .setReorderable(true)
                         .setOnSelectionChanged([this](const std::string& id) {
                             _selection->select(id);
                             uint64_t uuid = 0;
                             std::string entryId;
                             if (parseEditorHierarchyEntityIdKey(id, uuid)) {
                                 if (Scene* scene = _layer->getHierarchyScene()) {
                                     _layer->setSelectedEntity(scene->getEntityByUUID(uuid));
                                 }
                             }
                             else if (parseWidgetEntryKey(id, entryId)) {
                                 _layer->setSelectedWidgetEntryId(entryId);
                             }
                         })
                         .setOnReorderHandler([this](const std::string& fromId,
                                                     const std::string& toId,
                                                     int dropMode) {
                             if (!_layer) {
                                 return;
                             }
                             Scene* scene = _layer->getHierarchyScene();
                             if (!scene) {
                                 return;
                             }
                             if (Entity* moved = moveEditorHierarchyEntity(*scene, fromId, toId, dropMode)) {
                                 _layer->setSelectedEntity(moved);
                                 _hierarchyFingerprint.clear();
                             }
                         })
                         .setOnContextMenu([this](const std::string&, const glm::vec2& logicalPoint) {
                             openHierarchyContextMenu(logicalPoint);
                         })
                         .share();
    auto hierarchyScroll = ui::scroll("HierarchyScroll")
                               .child(_hierarchyView, ui::overlaySlot().fill());
    return ui::panel("HierarchyBody")
        .setStyleKey("panel.canvas")
        .child(std::move(hierarchyFilterField),
               ui::canvasSlot()
                   .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                   .offset({4.0f, 4.0f})
                   .size({0.0f, 26.0f}))
        .child(std::move(hierarchyScroll),
               ui::canvasSlot()
                   .anchor({0.0f, 0.0f}, {1.0f, 1.0f})
                   .offset({4.0f, 34.0f}))
        .release();
}

void EditorSurface::buildToolsMenu()
{
    if (!_menuBar) {
        return;
    }
    _menuBar->addItem("Tools", [this]() {
        std::vector<UIMenu::FItem> items;
        items.push_back({
            .label  = "Viewport",
            .action = [this]() { invokeTab("viewport"); },
        });
        items.push_back({
            .label  = "Hierarchy",
            .action = [this]() { invokeTab("hierarchy"); },
        });
        if (_tabSpawners) {
            for (const FEditorTabSpawner& spawner : _tabSpawners->all()) {
                if (spawner.toolsMenuLabel.empty()) {
                    continue;
                }
                const std::string tabId = spawner.tabId;
                items.push_back({
                    .label  = spawner.toolsMenuLabel,
                    .action = [this, tabId]() { invokeTab(tabId); },
                });
            }
        }
        return UIMenu::create(std::move(items));
    });
}

void EditorSurface::materializeWorkspaceTabs()
{
    nlohmann::json layout;
    std::vector<std::string> keys;
    if (ConfigManager::get().tryGet("editor", "dockLayout", layout)) {
        keys = FDockContext::collectLayoutPanelKeys(layout);
    }
    if (keys.empty()) {
        for (std::string_view id : kDefaultWorkspaceTabs) {
            keys.emplace_back(id);
        }
    }
    for (const std::string& id : keys) {
        materializeTab(id);
    }
}

bool EditorSurface::materializeTab(std::string_view tabId)
{
    if (!_dockContext || tabId.empty()) {
        return false;
    }
    if (_dockContext->hasPanel(tabId)) {
        return true;
    }
    std::shared_ptr<UIElement> widget;
    std::string title{tabId};
    if (tabId == "viewport") {
        widget = buildViewportBody();
        title = "Viewport";
    }
    else if (tabId == "hierarchy") {
        widget = buildHierarchyBody();
        title = "Hierarchy";
    }
    else if (_tabSpawners) {
        if (const FEditorTabSpawner* spawner = _tabSpawners->find(tabId)) {
            FEditorTabSpawnContext ctx = makeSpawnContext();
            widget = spawner->spawn(ctx);
            title = spawner->title;
        }
    }
    if (!widget) {
        YA_CORE_WARN("EditorSurface: no spawner for tab '{}'", tabId);
        return false;
    }
    return _dockContext->addPanel(std::string(tabId), title, std::move(widget)) != kInvalidDockPanelId;
}

bool EditorSurface::invokeTab(std::string_view tabId)
{
    if (!_dockContext) {
        return false;
    }
    if (_dockContext->hasPanel(tabId)) {
        return _dockContext->activatePanel(tabId);
    }
    if (!materializeTab(tabId)) {
        return false;
    }
    const FDockContext::FPanel* spawned = _dockContext->findPanelByStableKey(tabId);
    if (!spawned) {
        return false;
    }
    if (const FDockContext::FPanel* content = _dockContext->findPanelByStableKey("content-browser")) {
        if (FDockNode* leaf = _dockContext->dockModel().findLeafForPanel(content->id)) {
            (void)_dockContext->dockModel().movePanel(spawned->id, leaf->id);
        }
    }
    _dockContext->fireDockUpdated();
    return _dockContext->activatePanel(tabId);
}

void EditorSurface::applyWindowMetrics(App& app)
{
    int windowW = 0;
    int windowH = 0;
    auto* render = app.getRenderServices().getRender();
    if (render) {
        render->getWindowSize(windowW, windowH);
    }
    _tree->setLogicalExtent(Extent2D{
        .width  = static_cast<uint32_t>(std::max(windowW, 1)),
        .height = static_cast<uint32_t>(std::max(windowH, 1)),
    });

    float dpiScale = 1.0f;
    if (render) {
        if (auto* window = render->getNativeWindow()) {
            dpiScale = window->getDpiScale();
        }
        const uint32_t fbW = render->getSwapchainWidth();
        const uint32_t fbH = render->getSwapchainHeight();
        if (windowW > 0 && windowH > 0 && fbW > 0 && fbH > 0) {
            dpiScale = static_cast<float>(fbW) / static_cast<float>(windowW);
        }
    }
    _tree->setDpiScale(dpiScale);
    if (auto* fonts = FontManager::get()) {
        fonts->setActiveDpiScale(dpiScale);
    }
}

void EditorSurface::applyDefaultEditorDockLayout()
{
    if (!_dockContext) {
        return;
    }
    FDockTreeModel& model = _dockContext->dockModel();
    const DockPanelId viewportId = dockPanelIdForKey(model, "viewport");
    const DockPanelId hierarchyId = dockPanelIdForKey(model, "hierarchy");
    const DockPanelId inspectorId = dockPanelIdForKey(model, "inspector");
    const DockPanelId contentId = dockPanelIdForKey(model, "content-browser");
    const DockPanelId statsId = dockPanelIdForKey(model, "frame-stats");
    const DockPanelId workbenchId = dockPanelIdForKey(model, "gui-workbench");
    const DockPanelId runtimeId = dockPanelIdForKey(model, "runtime-tools");
    const DockPanelId designerId = dockPanelIdForKey(model, "ui-designer");
    const DockPanelId assetsId = dockPanelIdForKey(model, "asset-inspector");
    const DockPanelId debugId = dockPanelIdForKey(model, "debug-images");
    if (viewportId == kInvalidDockPanelId || hierarchyId == kInvalidDockPanelId || inspectorId == kInvalidDockPanelId ||
        contentId == kInvalidDockPanelId) {
        return;
    }

    model.selectPanel(viewportId);
    const DockNodeId rootLeaf = model.getRootNode()->id;
    model.splitLeaf(rootLeaf, EDockCardinalSide::East, inspectorId, 0.74f);
    if (FDockNode* viewportLeaf = model.findLeafForPanel(viewportId)) {
        model.splitLeaf(viewportLeaf->id, EDockCardinalSide::West, hierarchyId, 0.26f);
    }
    if (FDockNode* viewportLeaf = model.findLeafForPanel(viewportId)) {
        model.splitLeaf(viewportLeaf->id, EDockCardinalSide::South, contentId, 0.72f);
    }
    if (FDockNode* contentLeaf = model.findLeafForPanel(contentId)) {
        if (statsId != kInvalidDockPanelId) {
            model.movePanel(statsId, contentLeaf->id);
        }
        if (workbenchId != kInvalidDockPanelId) {
            model.movePanel(workbenchId, contentLeaf->id);
        }
        if (runtimeId != kInvalidDockPanelId) {
            model.movePanel(runtimeId, contentLeaf->id);
        }
        if (designerId != kInvalidDockPanelId) {
            model.movePanel(designerId, contentLeaf->id);
        }
        if (assetsId != kInvalidDockPanelId) {
            model.movePanel(assetsId, contentLeaf->id);
        }
        if (debugId != kInvalidDockPanelId) {
            model.movePanel(debugId, contentLeaf->id);
        }
        model.selectPanel(contentId);
    }
    model.selectPanel(viewportId);
    model.selectPanel(hierarchyId);
    model.selectPanel(inspectorId);
}

bool EditorSurface::tryRestoreEditorDockLayout()
{
    if (!_dockContext) {
        return false;
    }
    nlohmann::json layout = nlohmann::json::object();
    if (!ConfigManager::get().tryGet("editor", "dockLayout", layout)) {
        return false;
    }
    return _dockContext->importLayoutJson(layout);
}

void EditorSurface::persistEditorDockLayout()
{
    if (!_dockContext) {
        return;
    }
    ConfigManager::Editor("editor")
        .set("dockLayout", _dockContext->exportLayoutJson())
        .flush();
}

void EditorSurface::openViewportContextMenu(const glm::vec2& windowPoint)
{
    if (!_tree || !_layer || !_layer->canViewportAuthor()) {
        return;
    }

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
    menu->openAt(*_tree, windowPoint);
}

void EditorSurface::openHierarchyContextMenu(const glm::vec2& logicalPoint)
{
    if (!_tree || !_actions) {
        return;
    }
    auto menu = UIMenu::create({
        UIMenu::FItem::fromAction(*_actions, "selection.createEmpty"),
        UIMenu::FItem::separator(),
        UIMenu::FItem::fromAction(*_actions, "selection.duplicate"),
        UIMenu::FItem::fromAction(*_actions, "selection.delete"),
    });
    menu->openAt(*_tree, logicalPoint);
}

void EditorSurface::syncPresentation(App& app, float dt)
{
    (void)dt;
    if (_bBuiltAsProjectBrowser) {
        std::vector<UITreeView::FNode> projects;
        const auto& discovered = _layer->getDiscoveredProjects();
        projects.reserve(discovered.size());
        for (int i = 0; i < static_cast<int>(discovered.size()); ++i) {
            projects.push_back(UITreeView::FNode{
                .id    = std::to_string(i),
                .label = discovered[static_cast<size_t>(i)],
            });
        }
        if (_hierarchyRoots) {
            _hierarchyRoots->replace(std::move(projects));
        }
        if (_projectErrorText) {
            _projectErrorText->setText(_layer->getProjectBrowserError());
        }
        return;
    }

    syncViewportTexture();
    syncSelectionFromLayer();
    syncHierarchy();
    syncToolbar(app);
    if (_filePicker) {
        _filePicker->sync(*_tree);
    }
    if (_settings) {
        _settings->sync(*_tree);
    }
}

void EditorSurface::syncViewportTexture()
{
    if (!_viewportImage) {
        return;
    }
    const auto& display = _layer->getViewportDisplayImage();
    const bool expectsViewport = _layer->getHierarchyScene() != nullptr;
    if (!display || !display->isValid() || !display->getImageView()) {
        _viewportImage->setTexture(nullptr);
        _viewportImage->setResourceMissing(expectsViewport);
        _viewportTexture.reset();
        _viewportImageResource.reset();
        _viewportImageView.reset();
        return;
    }

    auto sourceImage     = display->getImageShared();
    auto sourceImageView = display->getImageViewShared();
    if (!sourceImage || !sourceImageView) {
        _viewportImage->setTexture(nullptr);
        _viewportImage->setResourceMissing(expectsViewport);
        return;
    }
    _viewportImage->setResourceMissing(false);
    if (_viewportTexture &&
        _viewportImageResource == sourceImage &&
        _viewportImageView == sourceImageView) {
        _viewportImage->setTexture(_viewportTexture);
        return;
    }

    _viewportImageResource = std::move(sourceImage);
    _viewportImageView     = std::move(sourceImageView);
    _viewportTexture       = Texture::wrap(_viewportImageResource,
                                     _viewportImageView,
                                     "EditorSurfaceViewport");
    _viewportImage->setTexture(_viewportTexture);
}

void EditorSurface::syncHierarchy()
{
    if (!_hierarchyRoots) {
        return;
    }
    Scene* scene = _layer->getHierarchyScene();
    std::string fingerprint;
    fingerprint.reserve(256);
    if (scene) {
        fingerprint += scene->getName();
        if (Node* root = scene->getRootNode()) {
            appendHierarchyFingerprint(fingerprint, root);
        }
        fingerprint += "|ui:";
        fingerprint += std::to_string(scene->getWidgetEntries().size());
        for (const auto& entry : scene->getWidgetEntries()) {
            fingerprint += entry.entryId;
            fingerprint += ';';
        }
    }
    if (fingerprint == _hierarchyFingerprint) {
        return;
    }
    _hierarchyFingerprint = std::move(fingerprint);

    std::vector<UITreeView::FNode> roots;
    if (scene) {
        if (Node* root = scene->getRootNode()) {
            for (Node* child : root->getChildren()) {
                roots.push_back(buildHierarchyNode(child));
            }
        }
        if (!scene->getWidgetEntries().empty()) {
            UITreeView::FNode uiRoot;
            uiRoot.id    = "ui-root";
            uiRoot.label = "Game UI";
            for (const auto& entry : scene->getWidgetEntries()) {
                uiRoot.children.push_back(UITreeView::FNode{
                    .id    = std::format("ui:{}", entry.entryId),
                    .label = entry.entryId,
                });
            }
            roots.push_back(std::move(uiRoot));
        }
    }
    _hierarchyRoots->replace(std::move(roots));
}

void EditorSurface::syncSelectionFromLayer()
{
    if (!_layer || !_selection) {
        return;
    }
    const uint64_t generation = _layer->selectionGeneration();
    if (generation == _syncedSelectionGeneration) {
        return;
    }
    _syncedSelectionGeneration = generation;

    std::vector<std::string> ids;
    if (!_layer->getSelectedWidgetEntryId().empty()) {
        ids.push_back(std::format("ui:{}", _layer->getSelectedWidgetEntryId()));
    }
    else {
        ids.reserve(_layer->getSelections().size());
        for (Entity* entity : _layer->getSelections()) {
            if (!entity || !entity->isValid()) {
                continue;
            }
            uint64_t uuid = 0;
            if (auto* id = entity->getComponent<IDComponent>()) {
                uuid = id->_id.value;
            }
            if (uuid != 0) {
                ids.push_back(editorHierarchyEntityIdKey(uuid));
            }
        }
    }
    std::string primary = ids.empty() ? std::string{} : ids.front();
    _selection->replace(std::move(ids), std::move(primary));
}

void EditorSurface::syncToolbar(App& app)
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
    if (!_viewportImage || !_layer) {
        return;
    }
    _layer->notifyViewportWidgetRect(_viewportImage->_layoutRect);
    const bool hovered = widgetOrAncestor(_tree->getHovered(), _viewportImage.get());
    const bool focused = widgetOrAncestor(_tree->getFocused(), _viewportImage.get()) || hovered;
    _layer->setViewportHoverFocus(hovered, focused);
}

void EditorSurface::syncViewportHostState(App& app)
{
    if (!_viewportImage) {
        return;
    }

    FEditorViewportHostState state{};
    state.widgetRect = _viewportImage->_layoutRect;
    state.extent     = state.widgetRect.extent;
    state.bHovered   = isViewportHovered();
    state.bFocused   = isViewportFocused();

    const auto& frameState = app.getRenderServices().getRenderFrameState();
    state.view             = frameState.view;
    state.projection       = frameState.projection;

    _viewportOverlayHost.syncHost(state);
}

EWidgetRouteResult EditorSurface::dispatchEvent(const Event& event, const glm::vec2& windowPoint)
{
    if (!_tree) {
        return EWidgetRouteResult::NotHandled;
    }

    if (_viewportImage && (isViewportHovered() || isViewportFocused())) {
        const glm::vec2 localPoint = windowPoint - _viewportImage->_layoutRect.pos;
        const EWidgetRouteResult overlayResult = _viewportOverlayHost.dispatchEvent(event, localPoint);
        if (overlayResult != EWidgetRouteResult::NotHandled) {
            return overlayResult;
        }
        if (event.getEventType() == EEvent::MouseButtonPressed) {
            const auto& mouseEvent = static_cast<const MouseButtonPressedEvent&>(event);
            if (mouseEvent.GetMouseButton() == EMouse::Right && _layer && _layer->canViewportAuthor() &&
                !_layer->isRightMouseDragging()) {
                openViewportContextMenu(windowPoint);
                return EWidgetRouteResult::HandledExclusive;
            }
        }
    }

    WidgetEventContext ctx;
    ctx.logicalPoint = windowPoint;
    const EWidgetRouteResult routed = _tree->dispatchEvent(event, ctx);
    if (routed != EWidgetRouteResult::NotHandled) {
        return routed;
    }
    if (event.getEventType() == EEvent::KeyPressed &&
        _actions->dispatchKey(static_cast<const KeyPressedEvent&>(event), wantsTextInput())) {
        return EWidgetRouteResult::HandledExclusive;
    }
    return routed;
}

bool EditorSurface::isViewportHovered() const
{
    return _tree && widgetOrAncestor(_tree->getHovered(), _viewportImage.get());
}

bool EditorSurface::isViewportFocused() const
{
    return _tree && widgetOrAncestor(_tree->getFocused(), _viewportImage.get());
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
