#include "GameEditor/UI/EditorSurface.h"

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
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GameEditor/UI/EditorTabRegistry.h"
#include "GameRuntime/App.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/SizeBox.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Widgets/KeyedVisibleWindow.h"
#include "GUI/Widgets/WidgetAttachment.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"
#include "Hierarchy/Node.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "GameRuntime/GUI/GuiSystem.h"
#include "RHI/Core/CommandBuffer.h"
#include "RHI/Render.h"
#include "Render/Resources/FontManager.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>

namespace ya
{

namespace
{

constexpr float kMenuHeight     = 30.0f;
constexpr float kToolbarHeight  = 36.0f;
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

std::shared_ptr<UIElement> makePlaceholderPanel(const std::string& name, const std::string& text)
{
    return ui::panel(name + "_Body")
        .setStyleKey("panel.canvas")
        .child(ui::text(name + "_Label")
                   .setText(text)
                   .setFontSize(13)
                   .setHAlign(EWidgetAlignH::Center)
                   .setVAlign(EWidgetAlignV::Center),
               ui::canvasSlot().fill().offset({12.0f, 12.0f}))
        .release();
}

ui::UIButtonWidgetBuilder labeledButton(std::string key, const std::string& label)
{
    std::string labelKey = key + "_Label";
    return ui::button(std::move(key))
        .child(ui::text(std::move(labelKey))
                   .setText(label)
                   .setFontSize(13)
                   .setHAlign(EWidgetAlignH::Center)
                   .setVAlign(EWidgetAlignV::Center));
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

UITreeView::FNode makeDesignerTreeNode(const UIElement& widget, const std::string& path)
{
    UITreeView::FNode node;
    node.id = path;
    node.label = widget._name + " [" + widget._typeId + "]";
    const auto& children = widget.getChildren();
    node.children.reserve(children.size());
    for (size_t index = 0; index < children.size(); ++index) {
        node.children.push_back(makeDesignerTreeNode(*children[index], path + "/" + std::to_string(index)));
    }
    return node;
}

void collectDesignerTreeFingerprint(const UIElement& widget, std::string& out, const std::string& path)
{
    out += path + ":" + widget._name + ":" + widget._typeId + ";";
    const auto& children = widget.getChildren();
    for (size_t index = 0; index < children.size(); ++index) {
        collectDesignerTreeFingerprint(*children[index], out, path + "/" + std::to_string(index));
    }
}

} // namespace

std::shared_ptr<UIElement> EditorSurface::buildAssetInspector(EditorLayer& layer)
{
    (void)layer;
    auto pathText = ui::text("AssetInspectorPath").setText("No asset selected").setStyleKey("text.muted").share();
    auto statusText = ui::text("AssetInspectorStatus").setText("Select a texture in Content Browser").setStyleKey("text.muted").share();
    auto preview = ui::image("AssetInspectorPreview").setStyleKey("image").share();
    _assetInspectorPathText = pathText;
    _assetInspectorStatusText = statusText;
    _assetInspectorPreview = preview;

    return ui::panel("AssetInspectorBody")
        .setStyleKey("panel.canvas")
        .child(ui::column("AssetInspectorColumn")
                   .setSpacing(8.0f)
                   .child(pathText)
                   .child(preview, FBoxSlotArgs{.preferredSize = {0.0f, 220.0f}})
                   .child(statusText)
                   .release(),
               ui::canvasSlot().fill().offset({12.0f, 12.0f}))
        .release();
}

std::shared_ptr<UIElement> EditorSurface::buildUIDesigner(EditorLayer& layer)
{
    auto status = ui::text("UIDesignerStatus").setText("No document open").setStyleKey("text.muted").share();
    auto selection = ui::text("UIDesignerSelection").setText("No widget selected").setStyleKey("text.muted").share();
    _uiDesignerRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _uiDesignerSelection = std::make_shared<Reactive<std::string>>("");
    auto treeBuilder = ui::treeView("UIDesignerTree")
                           .bindData(_uiDesignerRoots)
                           .bindSelection(_uiDesignerSelection)
                           .setOnSelectionChanged([&layer](const std::string& id) {
                               std::vector<size_t> path;
                               size_t start = 0;
                               while (start < id.size()) {
                                   const size_t slash = id.find('/', start);
                                   const size_t end = slash == std::string::npos ? id.size() : slash;
                                   if (start == 0 && id.compare(start, end - start, "root") == 0) {
                                       start = slash == std::string::npos ? id.size() : slash + 1;
                                       continue;
                                   }
                                   try {
                                       path.push_back(static_cast<size_t>(std::stoul(id.substr(start, end - start))));
                                   }
                                   catch (...) {
                                       return;
                                   }
                                   start = slash == std::string::npos ? id.size() : slash + 1;
                               }
                               layer.getUIDesignerPanel().selectByChildPath(path);
                           });
    auto tree = treeBuilder.share();
    _uiDesignerTree = tree;

    auto newBuilder = ui::button("UIDesignerNew").child(ui::text("UIDesignerNewLabel").setText("New Panel"));
    newBuilder.setOnClick([&layer]() { layer.getUIDesignerPanel().newDocument("panel"); });
    auto newButton = newBuilder.share();

    auto saveBuilder = ui::button("UIDesignerSave").child(ui::text("UIDesignerSaveLabel").setText("Save"));
    saveBuilder.setOnClick([&layer]() { (void)layer.getUIDesignerPanel().saveDocument(); });
    auto saveButton = saveBuilder.share();

    auto closeBuilder = ui::button("UIDesignerClose").child(ui::text("UIDesignerCloseLabel").setText("Close"));
    closeBuilder.setOnClick([&layer]() { layer.getUIDesignerPanel().clearDocument(); });
    auto closeButton = closeBuilder.share();

    _uiDesignerStatusText = status;
    _uiDesignerSelectionText = selection;
    _uiDesignerNewButton = newButton;
    _uiDesignerSaveButton = saveButton;
    _uiDesignerCloseButton = closeButton;

    return ui::panel("UIDesignerBody")
        .setStyleKey("panel.canvas")
        .child(ui::column("UIDesignerColumn")
                   .setSpacing(8.0f)
                   .child(status)
                   .child(selection)
                   .child(tree, FBoxSlotArgs{.preferredSize = {0.0f, 220.0f}})
                   .child(newButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .child(saveButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .child(closeButton, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .release(),
               ui::canvasSlot().fill().offset({12.0f, 12.0f}))
        .release();
}

std::shared_ptr<UIElement> EditorSurface::buildRuntimeTools(EditorLayer& layer)
{
    auto status = ui::text("RuntimeToolsStatus").setText("Stopped").setStyleKey("text.header").share();
    auto frame = ui::text("RuntimeToolsFrame").setText("Frame 0").setStyleKey("text.muted").share();

    auto playBuilder = ui::button("RuntimeToolsPlay").child(ui::text("RuntimeToolsPlayLabel").setText("Play"));
    playBuilder.setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startRuntime(); });
        }
    });
    auto play = playBuilder.share();

    auto simulateBuilder = ui::button("RuntimeToolsSimulate").child(ui::text("RuntimeToolsSimulateLabel").setText("Simulate"));
    simulateBuilder.setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startSimulation(); });
        }
    });
    auto simulate = simulateBuilder.share();

    auto stopBuilder = ui::button("RuntimeToolsStop").child(ui::text("RuntimeToolsStopLabel").setText("Stop"));
    stopBuilder.setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() {
                if (app->isRuntimeMode()) app->stopRuntime();
                else if (app->isSimulationMode()) app->stopSimulation();
            });
        }
    });
    auto stop = stopBuilder.share();

    _runtimeToolsStatusText = status;
    _runtimeToolsFrameText = frame;
    _runtimeToolsPlayButton = play;
    _runtimeToolsSimulateButton = simulate;
    _runtimeToolsStopButton = stop;

    return ui::panel("RuntimeToolsBody")
        .setStyleKey("panel.canvas")
        .child(ui::column("RuntimeToolsColumn")
                   .setSpacing(8.0f)
                   .child(status)
                   .child(frame)
                   .child(play, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .child(simulate, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .child(stop, FBoxSlotArgs{.preferredSize = {140.0f, 26.0f}})
                   .release(),
               ui::canvasSlot().fill().offset({12.0f, 12.0f}))
        .release();
}

EditorSurface::~EditorSurface() = default;

void EditorSurface::shutdown()
{
    _workbench.reset();
    _tabRegistry.reset();
    clearSceneSaveDialog();
    _tree.reset();
    _theme.reset();
    _snapshot = {};
    _root.reset();
    _menuBar.reset();
    _toolbarModeText.reset();
    _dockWorkspace.reset();
    _dockSpace.reset();
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
    _inspectorTab.reset();
    _statsText.reset();
    _contentExplorer.reset();
    _contentPathText.reset();
    _contentSearchField.reset();
    _contentMountList.reset();
    _contentEntryList.reset();
    _contentEntryRows.reset();
    _contentEntryLeading.reset();
    _contentEntryTrailing.reset();
    _contentEntryScroll.reset();
    _contentMountReconciler.reset();
    _contentEntryReconciler.reset();
    _contentFingerprint.clear();
    _contentEntryScrollOffset = 0.0f;
    _contentEntryViewportHeight = 0.0f;
    _bContentRowsDirty = true;
    _viewportTexture.reset();
    _viewportImageResource.reset();
    _viewportImageView.reset();
    _hierarchyFingerprint.clear();
    _syncedSelectionGeneration = ~uint64_t{0};
    _layer = nullptr;
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
    _dockWorkspace.reset();
    _dockSpace.reset();
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
    _inspectorTab.reset();
    _statsText.reset();
    _workbench.reset();
    _tabRegistry = std::make_unique<EditorTabRegistry>();
    _contentExplorer.reset();
    _contentPathText.reset();
    _contentSearchField.reset();
    _contentMountList.reset();
    _contentEntryList.reset();
    _contentEntryRows.reset();
    _contentEntryLeading.reset();
    _contentEntryTrailing.reset();
    _contentEntryScroll.reset();
    _contentMountReconciler.reset();
    _contentEntryReconciler.reset();
    _contentFingerprint.clear();
    _contentEntryScrollOffset = 0.0f;
    _contentEntryViewportHeight = 0.0f;
    _bContentRowsDirty = true;
    clearSceneSaveDialog();
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
    _statsText     = errorText.share();

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
        .id      = "app.exit",
        .label   = "Exit",
        .execute = []() {
            if (auto* app = App::get()) {
                app->requestQuit();
            }
        },
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
        });
    });
    _menuBar->addItem("View", [this]() {
        return UIMenu::create({
            UIMenu::FItem::fromAction(*_actions, "viewport.mode3d"),
            UIMenu::FItem::fromAction(*_actions, "viewport.mode2d"),
        });
    });

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
                  .setSpacing(8.0f)
                  .setPadding({8.0f, 4.0f})
                  .child(std::move(play), ui::boxSlot().preferredSize({72.0f, 28.0f}))
                  .child(std::move(simulate), ui::boxSlot().preferredSize({88.0f, 28.0f}))
                  .child(std::move(stop), ui::boxSlot().preferredSize({72.0f, 28.0f}))
                  .child(std::move(mode3d), ui::boxSlot().preferredSize({44.0f, 28.0f}))
                  .child(std::move(mode2d), ui::boxSlot().preferredSize({44.0f, 28.0f}))
                  .child(std::move(modeText), ui::boxSlot().preferredSize({88.0f, 20.0f})),
              ui::canvasSlot()
                  .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                  .offset({0.0f, kMenuHeight})
                  .size({0.0f, kToolbarHeight}));

    _dockWorkspace = std::make_shared<UIDockWorkspace>();
    _dockSpace = ui::buildAs<UIDockSpace>(*_tree,
                                          *_root,
                                          ui::dockSpace("EditorDock").setWorkspace(_dockWorkspace),
                                          ui::canvasSlot().anchor({0.0f, 0.0f}, {1.0f, 1.0f}).offset({0.0f, kChromeTop}));

    auto viewportImage = ui::image("ViewportImage");
    _viewportImage     = viewportImage.share();
    _viewportImage->_hitFilter = EWidgetHitFilter::Stop;
    auto viewportBody = ui::panel("ViewportBody")
                            .setStyleKey("panel.canvas")
                            .child(std::move(viewportImage), ui::canvasSlot().fill());

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
                         .share();
    auto hierarchyScroll = ui::scroll("HierarchyScroll")
                               .child(_hierarchyView, ui::overlaySlot().fill());
    auto hierarchyBody = ui::panel("HierarchyBody")
                             .setStyleKey("panel.canvas")
                             .child(std::move(hierarchyFilterField),
                                    ui::canvasSlot()
                                        .anchor({0.0f, 0.0f}, {1.0f, 0.0f})
                                        .offset({4.0f, 4.0f})
                                        .size({0.0f, 26.0f}))
                             .child(std::move(hierarchyScroll),
                                    ui::canvasSlot()
                                        .anchor({0.0f, 0.0f}, {1.0f, 1.0f})
                                        .offset({4.0f, 34.0f}));

    _inspectorTab = std::make_unique<EditorInspectorTab>(*_layer, _undo.get());
    auto inspectorBody = _inspectorTab->build(*_tree);

    auto statsText = ui::text("FrameStatsBody")
                         .setText("Frame Stats")
                         .setFontSize(13);
    _statsText = statsText.share();
    auto statsBody = ui::panel("FrameStatsPanel")
                         .setStyleKey("panel.canvas")
                         .child(std::move(statsText), ui::canvasSlot().fill().offset({12.0f, 12.0f}));

    auto workbenchHost = ui::panel("WorkbenchHost").setStyleKey("panel.window").share();
    // Dock panel bodies are detached until their tab is selected. WorkbenchSurface
    // authors with tree.attach, so mount the host long enough to build, then
    // detach the intact subtree for the workspace to graft later.
    _workbench = std::make_unique<guiworkbench::FWorkbenchSurface>();
    {
        const WidgetAttachment hostAttached = _tree->attach(*_root, workbenchHost);
        YA_CORE_ASSERT(hostAttached.valid(), "EditorSurface: failed to attach workbench host for authoring");
        _workbench->buildUI(*_tree, *workbenchHost);
        _tree->detach(*workbenchHost);
    }

    const DockPanelId viewportId  = _dockWorkspace->addPanel("Viewport", viewportBody.release());
    const DockPanelId hierarchyId = _dockWorkspace->addPanel("Hierarchy", hierarchyBody.release());
    const DockPanelId inspectorId = _dockWorkspace->addPanel("Inspector", inspectorBody);
    const DockPanelId contentId   = _dockWorkspace->addPanel("Content Browser", buildContentBrowser());
    const DockPanelId statsId     = _dockWorkspace->addPanel("Frame Stats", statsBody.release());
    const DockPanelId workbenchId = _dockWorkspace->addPanel("GUI Workbench", workbenchHost);
    _tabRegistry->registerTab({
        .id = "runtime-tools",
        .title = "Runtime Tools",
        .build = [this](EditorLayer& layer, WidgetTree&) { return buildRuntimeTools(layer); },
        .sync = [this](EditorLayer&, WidgetTree&) {
            if (!_runtimeToolsStatusText || !_runtimeToolsFrameText) {
                return;
            }
            App* app = App::get();
            if (!app) {
                return;
            }
            const char* state = app->isRuntimeMode() ? "Playing" : (app->isSimulationMode() ? "Simulating" : "Stopped");
            _runtimeToolsStatusText->setText(state);
            _runtimeToolsFrameText->setText(std::format("Frame {}", app->getFrameIndex()));
            if (_runtimeToolsPlayButton) _runtimeToolsPlayButton->setEnabled(app->isStopped());
            if (_runtimeToolsSimulateButton) _runtimeToolsSimulateButton->setEnabled(app->isStopped());
            if (_runtimeToolsStopButton) _runtimeToolsStopButton->setEnabled(!app->isStopped());
        },
    });
    _tabRegistry->registerTab({
        .id = "ui-designer",
        .title = "UI Designer",
        .build = [this](EditorLayer& layer, WidgetTree&) { return buildUIDesigner(layer); },
        .sync = [this](EditorLayer& layer, WidgetTree&) {
            if (!_uiDesignerStatusText || !_uiDesignerSelectionText || !_uiDesignerRoots || !_uiDesignerSelection) {
                return;
            }
            const auto& designer = layer.getUIDesignerPanel();
            const auto& document = designer.getOpenDocument();
            _uiDesignerStatusText->setText(document ? "Document: " + document->typeId : "No document open");
            UIElement* selected = designer.getSelectedWidget();
            _uiDesignerSelectionText->setText(selected ? "Selected: " + selected->_name : "No widget selected");
            std::string fingerprint;
            std::vector<UITreeView::FNode> roots;
            if (UIElement* root = designer.getPreviewRoot()) {
                collectDesignerTreeFingerprint(*root, fingerprint, "root");
                roots.push_back(makeDesignerTreeNode(*root, "root"));
            }
            if (fingerprint != _uiDesignerTreeFingerprint) {
                _uiDesignerTreeFingerprint = std::move(fingerprint);
                _uiDesignerRoots->replace(std::move(roots));
            }
            if (_uiDesignerSaveButton) _uiDesignerSaveButton->setEnabled(document != nullptr);
            if (_uiDesignerCloseButton) _uiDesignerCloseButton->setEnabled(document != nullptr);
        },
    });
    _tabRegistry->registerTab({
        .id = "asset-inspector",
        .title = "Asset Inspector",
        .build = [this](EditorLayer& layer, WidgetTree&) { return buildAssetInspector(layer); },
        .sync = [this](EditorLayer& layer, WidgetTree&) {
            if (!_assetInspectorPathText || !_assetInspectorStatusText || !_assetInspectorPreview) {
                return;
            }
            const std::string& path = layer.getAssetInspectorPanel().inspectedPath();
            _assetInspectorPathText->setText(path.empty() ? "No asset selected" : path);
            _assetInspectorStatusText->setText(path.empty() ? "Select a texture in Content Browser" : "Texture preview");
            _assetInspectorPreview->_assetPath = path;
            _assetInspectorPreview->setResourceMissing(false);
        },
    });
    DockPanelId runtimeId = kInvalidDockPanelId;
    DockPanelId designerId = kInvalidDockPanelId;
    DockPanelId assetsId = kInvalidDockPanelId;
    for (const auto& tab : _tabRegistry->tabs()) {
        const DockPanelId id = _dockWorkspace->addPanel(tab.title, tab.build(*_layer, *_tree));
        if (tab.id == "runtime-tools") runtimeId = id;
        else if (tab.id == "ui-designer") designerId = id;
        else if (tab.id == "asset-inspector") assetsId = id;
    }

    auto& model = _dockWorkspace->dockModel();
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
        model.movePanel(statsId, contentLeaf->id);
        model.movePanel(workbenchId, contentLeaf->id);
        model.movePanel(runtimeId, contentLeaf->id);
        model.movePanel(designerId, contentLeaf->id);
        model.movePanel(assetsId, contentLeaf->id);
        model.selectPanel(contentId);
    }
    model.selectPanel(viewportId);
    model.selectPanel(hierarchyId);
    model.selectPanel(inspectorId);
    _dockWorkspace->fireDockUpdated();

    _viewportGizmoOverlay = std::make_shared<EditorViewportGizmoOverlay>(*_layer);
    _viewportOverlayHost.setOverlay(_viewportGizmoOverlay);
    _layer->setViewportGizmoUndoStack(_undo.get());
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

std::shared_ptr<UIElement> EditorSurface::buildContentBrowser()
{
    _contentExplorer = std::make_shared<FileExplorer>();
    _contentExplorer->setConfigScope("editorContentBrowser");
    _contentExplorer->initFromVFS();
    _contentExplorer->setFilterMode(FileExplorer::FilterMode::Both);
    _contentExplorer->setSelectionMode(FileExplorer::SelectionMode::File);
    _contentExplorer->setLeftPanelWidth(180.0f);

    auto pathText = ui::text("ContentPath").setFontSize(12).setVAlign(EWidgetAlignV::Center);
    _contentPathText = pathText.share();

    // Row lists are only rebuilt when the directory fingerprint or the entry
    // scroll window changes. Mounts stay fully materialized; entries use a
    // keyed visible window plus leading/trailing spacers so scroll extent
    // stays equal to the full directory height.
    _contentMountList = ui::column("ContentMounts").setSpacing(kEditorListRowSpacing).share();
    auto entryLeading = ui::sizeBox("ContentEntryLeading");
    _contentEntryLeading = entryLeading.share();
    auto entryRows = ui::column("ContentEntryRows").setSpacing(kEditorListRowSpacing);
    _contentEntryRows = entryRows.share();
    auto entryTrailing = ui::sizeBox("ContentEntryTrailing");
    _contentEntryTrailing = entryTrailing.share();
    _contentEntryList = ui::column("ContentEntries")
                            .setSpacing(0.0f)
                            .child(std::move(entryLeading))
                            .child(std::move(entryRows))
                            .child(std::move(entryTrailing))
                            .share();
    _contentMountReconciler.reset();
    _contentEntryReconciler.reset();
    _contentEntryScrollOffset = 0.0f;
    _contentEntryViewportHeight = 0.0f;

    auto backButton = ui::button("ContentBack")
                          .setOnClick([this]() {
                              if (_contentExplorer) {
                                  _contentExplorer->navigateBack();
                              }
                          })
                          .child(ui::text("ContentBack_Label")
                                     .setText("< Back")
                                     .setFontSize(12)
                                     .setHAlign(EWidgetAlignH::Center)
                                     .setVAlign(EWidgetAlignV::Center));

    auto searchField = ui::textField("ContentSearch")
                           .setOnTextChanged([this](const std::string& text) {
                               if (_contentExplorer) {
                                   _contentExplorer->setSearchText(text);
                                   _bContentRowsDirty = true;
                               }
                           });
    _contentSearchField = searchField.share();

    // Header: back / current path / name filter.
    auto header = ui::row("ContentBrowser.ContainerHeader", "Header")
                      .setSpacing(6.0f)
                      .child(std::move(backButton), ui::boxSlot().preferredSize({52.0f, 22.0f}))
                      .child(std::move(pathText))
                      .child(std::move(searchField), ui::boxSlot().preferredSize({140.0f, 22.0f}));

    // Body: mount list (fixed width) + entry list (fill). Scroll hosts keep
    // long directory listings from overflowing and give wheel navigation;
    // each host owns exactly the one list container.
    auto mountScroll = ui::scroll("ContentMountScroll")
                           .child(_contentMountList, ui::overlaySlot().fill());
    auto entryScroll = ui::scroll("ContentEntryScroll")
                           .child(_contentEntryList, ui::overlaySlot().fill());
    _contentEntryScroll = entryScroll.share();
    auto body = ui::row("ContentBody")
                    .setSpacing(4.0f)
                    .child(std::move(mountScroll), ui::boxSlot().preferredSize({180.0f, 0.0f}))
                    .child(std::move(entryScroll), ui::boxSlot().fill());

    auto root = ui::column("ContentBrowserRoot")
                    .setSpacing(2.0f)
                    .setPadding({4.0f, 4.0f})
                    .child(std::move(header), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                    .child(std::move(body), ui::boxSlot().fill());
    return root.release();
}

void EditorSurface::syncPresentation(App& app, float dt)
{
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
        if (_statsText) {
            _statsText->setText(_layer->getProjectBrowserError());
        }
        return;
    }

    syncViewportTexture();
    syncSelectionFromLayer();
    syncHierarchy();
    if (_inspectorTab) {
        _inspectorTab->sync(*_tree);
    }
    syncToolbar(app);
    syncContentBrowser();
    syncSceneSaveDialog();
    if (_tabRegistry) {
        for (const auto& tab : _tabRegistry->tabs()) {
            if (tab.sync) {
                tab.sync(*_layer, *_tree);
            }
        }
    }
    if (_statsText) {
        const float fps = dt > 0.0f ? 1.0f / dt : 0.0f;
        _statsText->setText(std::format(
            "Frame {}\nDelta {:.2f} ms\nFPS {:.1f}\nViewport {:.0f} x {:.0f}",
            app.getFrameIndex(),
            dt * 1000.0f,
            fps,
            _layer->getViewportSize().x,
            _layer->getViewportSize().y));
    }
    if (_workbench) {
        _workbench->updateUI();
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

void EditorSurface::syncContentBrowser()
{
    if (!_contentExplorer || !_contentPathText || !_tree) {
        return;
    }

    // Fingerprint: mount list identity + active mount + current directory +
    // entry summary. Rows are only rebuilt when one of these actually changed,
    // so typing in the search field or walking directories does not detach/
    // re-attach rows every frame.
    std::string fingerprint;
    if (const FileExplorer::MountPoint* active = _contentExplorer->getActiveMountPoint()) {
        fingerprint += active->name;
        fingerprint += '|';
    }
    fingerprint += _contentExplorer->getCurrentDirectory().string();
    fingerprint += '|';

    std::vector<FileExplorer::FEntry> entries;
    _contentExplorer->collectEntries(entries);
    for (const auto& entry : entries) {
        fingerprint += entry.name;
        fingerprint += entry.bIsDirectory ? "/" : ";";
    }
    fingerprint += "|search:";
    fingerprint += _contentExplorer->getSearchText();
    fingerprint += "|selected:";
    fingerprint += _contentExplorer->getSelectedPath().string();

    if (fingerprint != _contentFingerprint) {
        _contentFingerprint = std::move(fingerprint);
        _bContentRowsDirty = true;
        if (_contentEntryScroll) {
            _contentEntryScroll->setScrollOffset(0.0f);
            _contentEntryScrollOffset = 0.0f;
        }
    }
    if (_contentEntryScroll && _contentEntryScroll->isAttached()) {
        const float offset = _contentEntryScroll->getScrollOffset();
        const float viewportHeight = _contentEntryScroll->getLayoutRect().extent.y;
        if (offset != _contentEntryScrollOffset || viewportHeight != _contentEntryViewportHeight) {
            _contentEntryScrollOffset = offset;
            _contentEntryViewportHeight = viewportHeight;
            _bContentRowsDirty = true;
        }
    }
    if (_bContentRowsDirty && _contentMountList && _contentEntryList && _contentEntryRows &&
        _contentMountList->isAttached() && _contentEntryList->isAttached() && _contentEntryRows->isAttached()) {
        rebuildContentRows();
        _bContentRowsDirty = false;
    }
    if (_contentSearchField && _tree->getFocused() != _contentSearchField.get()) {
        _contentSearchField->setText(_contentExplorer->getSearchText());
    }
}

void EditorSurface::rebuildContentRows()
{
    if (!_contentExplorer || !_tree || !_contentMountList || !_contentEntryRows) {
        return;
    }

    const FileExplorer::MountPoint* active = _contentExplorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _contentExplorer->getSelectedPath();
    std::vector<FileExplorer::FEntry> entries;
    _contentExplorer->collectEntries(entries);

    if (!_contentMountReconciler) {
        _contentMountReconciler = std::make_unique<UIKeyedChildReconciler>(
            *_tree, *_contentMountList, makeContentRowFactory());
    }
    if (!_contentEntryReconciler) {
        _contentEntryReconciler = std::make_unique<UIKeyedChildReconciler>(
            *_tree, *_contentEntryRows, makeContentRowFactory());
    }

    std::vector<std::string> mountKeys;
    mountKeys.reserve(_contentExplorer->getMountPoints().size());
    for (const auto& mp : _contentExplorer->getMountPoints()) {
        mountKeys.push_back("ContentMount_" + mp.name);
    }
    _contentMountReconciler->reconcile(
        mountKeys,
        [this, active](UIElement& child, const std::string&, size_t index) {
            const auto& mp = _contentExplorer->getMountPoints()[index];
            updateContentRow(child,
                             mp.name,
                             mp.name,
                             active != nullptr && active->name == mp.name,
                             [this](const std::string& itemId) { selectContentMount(itemId); },
                             [this](const std::string& itemId) { selectContentMount(itemId); });
        },
        bindEditorListRowSlot);

    std::vector<std::string> entryKeys;
    entryKeys.reserve(entries.size());
    for (const auto& entry : entries) {
        entryKeys.push_back("ContentEntry_" + entry.name);
    }
    const FKeyedVisibleWindow window = computeKeyedVisibleWindow(entryKeys.size(),
                                                                kEditorListRowHeight,
                                                                kEditorListRowSpacing,
                                                                _contentEntryViewportHeight,
                                                                _contentEntryScrollOffset,
                                                                kEditorListOverscan);
    if (_contentEntryLeading) {
        _contentEntryLeading->setHeightOverride(window.leadingExtent);
    }
    if (_contentEntryTrailing) {
        _contentEntryTrailing->setHeightOverride(window.trailingExtent);
    }
    const std::vector<std::string> visibleKeys = sliceKeyedVisibleWindow(entryKeys, window);
    _contentEntryReconciler->reconcile(
        visibleKeys,
        [this, &entries, window, selectedPath](UIElement& child, const std::string&, size_t index) {
            const size_t itemIndex = window.first + index;
            const auto& entry = entries[itemIndex];
            const std::filesystem::path path = entry.path;
            const bool bDir = entry.bIsDirectory;
            updateContentRow(child,
                             bDir ? entry.name + "/" : entry.name,
                             entry.name,
                             selectedPath == path,
                             [this, path, bDir](const std::string&) { selectContentItem(path, bDir); },
                             [this, path, bDir](const std::string&) { activateContentItem(path, bDir); });
        },
        bindEditorListRowSlot);

    if (_contentPathText) {
        std::string pathText = _contentExplorer->getCurrentDirectory().string();
        if (active) {
            pathText = active->name + ": " + pathText;
        }
        _contentPathText->setText(pathText);
    }
}

void EditorSurface::selectContentMount(const std::string& itemId)
{
    if (!_contentExplorer) {
        return;
    }
    for (const auto& candidate : _contentExplorer->getMountPoints()) {
        if (candidate.name == itemId) {
            _contentExplorer->selectMountPoint(candidate);
            break;
        }
    }
}

void EditorSurface::selectContentItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_contentExplorer) {
        return;
    }
    _contentExplorer->setSelectedPath(path);
    _bContentRowsDirty = true;
    if (!_layer || bIsDirectory) {
        return;
    }

    std::string assetPath = path_utils::pathToUtf8String(path);
    if (VirtualFileSystem* vfs = VirtualFileSystem::get()) {
        assetPath = vfs->toVfsPath(assetPath);
    }
    const auto isTexturePath = [](std::string_view value) {
        return value.ends_with(".png") || value.ends_with(".jpg") || value.ends_with(".jpeg") ||
               value.ends_with(".tga") || value.ends_with(".bmp") || value.ends_with(".hdr");
    };
    if (isTexturePath(assetPath)) {
        _layer->inspectAsset(assetPath);
    }
}

void EditorSurface::activateContentItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_contentExplorer) {
        return;
    }
    if (bIsDirectory) {
        _contentExplorer->navigateInto(path);
        return;
    }
    // Scene files open through the scene services (frame task so the open
    // lands outside the input dispatch), everything else just selects.
    std::string utf8Path = path_utils::pathToUtf8String(path);
    if (utf8Path.ends_with(".scene.json")) {
        if (App* app = App::get()) {
            const std::string scenePath = std::move(utf8Path);
            app->getTaskManager().registerFrameTask([scenePath]() {
                App::get()->getSceneServices().loadScene(scenePath);
            });
        }
    }
}

void EditorSurface::openSceneSaveDialog()
{
    if (!_tree || !_root || !_layer) {
        return;
    }
    if (_sceneSaveOverlay && _sceneSaveOverlay->isAttached()) {
        _tree->setFocus(_sceneSaveNameField.get());
        return;
    }

    clearSceneSaveDialog();

    std::string defaultName = "NewScene";
    if (Scene* scene = _layer->getHierarchyScene(); scene && !scene->getName().empty()) {
        defaultName = scene->getName();
    }

    _sceneSaveExplorer = std::make_shared<FileExplorer>();
    _sceneSaveExplorer->setConfigScope("sceneSaveDialog");
    _sceneSaveExplorer->initFromVFS();
    _sceneSaveExplorer->setExtensions({});
    _sceneSaveExplorer->setFilterMode(FileExplorer::FilterMode::Directories);
    _sceneSaveExplorer->setSelectionMode(FileExplorer::SelectionMode::Directory);
    _sceneSaveExplorer->setLeftPanelWidth(180.0f);

    if (!_layer->getCurrentScenePath().empty()) {
        _sceneSaveExplorer->setSelectedPath(std::filesystem::path(_layer->getCurrentScenePath()).parent_path());
    }
    else {
        const std::string lastSaveDirectory = ConfigManager::get().getOr<std::string>("editor",
                                                                                      "sceneSaveDialog.lastDirectory",
                                                                                      "");
        if (!lastSaveDirectory.empty()) {
            _sceneSaveExplorer->setSelectedPath(path_utils::pathFromUtf8String(lastSaveDirectory));
        }
    }

    auto nameField = ui::textField("SceneSaveName").setText(defaultName);
    _sceneSaveNameField = nameField.share();

    auto pathText = ui::text("SceneSavePath").setFontSize(12).setStyleKey("text.muted");
    _sceneSavePathText = pathText.share();

    auto previewText = ui::text("SceneSavePreview").setFontSize(12);
    _sceneSavePreviewText = previewText.share();

    _sceneSaveMountList = ui::column("SceneSaveMounts").setSpacing(2.0f).share();
    _sceneSaveEntryList = ui::column("SceneSaveEntries").setSpacing(2.0f).share();
    _sceneSaveMountReconciler.reset();
    _sceneSaveEntryReconciler.reset();

    _sceneSaveSaveButton = labeledButton("SceneSaveConfirm", "Save")
                               .setOnClick([this]() { confirmSceneSaveDialog(); })
                               .share();

    auto nameRow = ui::row("SceneSaveNameRow")
                       .setSpacing(6.0f)
                       .setStretchLastChild(true)
                       .child(ui::text("SceneSaveNameLabel")
                                  .setText("Scene Name")
                                  .setFontSize(12)
                                  .setVAlign(EWidgetAlignV::Center), ui::boxSlot().preferredSize({90.0f, 26.0f}))
                       .child(std::move(nameField), ui::boxSlot().fill());
    auto saveBody = ui::row("SceneSaveBody")
                        .setSpacing(6.0f)
                        .setStretchLastChild(true)
                        .child(ui::scroll("SceneSaveMountScroll")
                                   .setAxis(EScrollAxis::Vertical)
                                   .child(_sceneSaveMountList, ui::overlaySlot().fill()),
                               ui::boxSlot().preferredSize({180.0f, 0.0f}))
                        .child(ui::scroll("SceneSaveEntryScroll")
                                   .setAxis(EScrollAxis::Vertical)
                                   .child(_sceneSaveEntryList, ui::overlaySlot().fill()),
                               ui::boxSlot().fill());
    auto actions = ui::row("SceneSaveActions")
                       .setSpacing(8.0f)
                       .setMainAxisAlignment(EWidgetMainAxisAlignment::End)
                       .child(labeledButton("SceneSaveBack", "Back")
                                  .setOnClick([this]() {
                                      if (_sceneSaveExplorer && _sceneSaveExplorer->navigateBack()) _bSceneSaveRowsDirty = true;
                                  }), ui::boxSlot().preferredSize({72.0f, 26.0f}))
                       .child(_sceneSaveSaveButton, ui::boxSlot().preferredSize({84.0f, 26.0f}))
                       .child(labeledButton("SceneSaveCancel", "Cancel")
                                  .setOnClick([this]() {
                                      if (_sceneSaveOverlay) _sceneSaveOverlay->close();
                                  }), ui::boxSlot().preferredSize({84.0f, 26.0f}));
    auto saveRoot = ui::column("SceneSaveRoot")
                       .setSpacing(8.0f)
                       .setPadding({12.0f, 12.0f})
                       .child(ui::text("SceneSaveTitle").setText("Save Scene").setStyleKey("text.header").setFontSize(14))
                       .child(std::move(nameRow), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                       .child(std::move(pathText))
                       .child(ui::textField("SceneSaveSearch")
                                  .setOnTextChanged([this](const std::string& text) {
                                      if (_sceneSaveExplorer) { _sceneSaveExplorer->setSearchText(text); _bSceneSaveRowsDirty = true; }
                                  }), ui::boxSlot().preferredSize({0.0f, 24.0f}))
                       .child(std::move(saveBody), ui::boxSlot().preferredSize({0.0f, 360.0f}))
                       .child(std::move(previewText))
                       .child(std::move(actions));
    auto dialogPanel = ui::panel("SceneSavePanel")
            .setStyleKey("panel.window")
            .child(std::move(saveRoot), ui::canvasSlot().fill());

    _sceneSavePanel = dialogPanel.share();
    _sceneSaveOverlay = ui::popupOverlay("SceneSaveOverlay")
                            .setRole(UIPopupOverlay::EOverlayRole::Modal)
                            .setOnDismiss([this]() { clearSceneSaveDialog(); })
                            .child(std::move(dialogPanel))
                            .share();

    _sceneSaveFingerprint.clear();
    _bSceneSaveRowsDirty = true;
    _sceneSaveOverlay->open(*_tree);
    _tree->setFocus(_sceneSaveNameField.get());
}

void EditorSurface::clearSceneSaveDialog()
{
    _sceneSaveOverlay.reset();
    _sceneSavePanel.reset();
    _sceneSaveExplorer.reset();
    _sceneSavePathText.reset();
    _sceneSavePreviewText.reset();
    _sceneSaveNameField.reset();
    _sceneSaveSaveButton.reset();
    _sceneSaveMountList.reset();
    _sceneSaveEntryList.reset();
    _sceneSaveMountReconciler.reset();
    _sceneSaveEntryReconciler.reset();
    _sceneSaveFingerprint.clear();
    _bSceneSaveRowsDirty = true;
}

void EditorSurface::syncSceneSaveDialog()
{
    if (!_sceneSaveOverlay || !_sceneSaveExplorer || !_sceneSavePanel || !_tree) {
        return;
    }

    std::string fingerprint;
    if (const FileExplorer::MountPoint* active = _sceneSaveExplorer->getActiveMountPoint()) {
        fingerprint += active->name;
        fingerprint += '|';
    }
    fingerprint += _sceneSaveExplorer->getCurrentDirectory().string();
    fingerprint += '|';
    fingerprint += _sceneSaveExplorer->getSelectedPath().string();

    std::vector<FileExplorer::FEntry> entries;
    _sceneSaveExplorer->collectEntries(entries);
    for (const auto& entry : entries) {
        fingerprint += entry.name;
        fingerprint += ';';
    }
    fingerprint += "|search:";
    fingerprint += _sceneSaveExplorer->getSearchText();
    if (_sceneSaveNameField) {
        fingerprint += "|name:";
        fingerprint += _sceneSaveNameField->getText();
    }

    if (fingerprint != _sceneSaveFingerprint) {
        _sceneSaveFingerprint = std::move(fingerprint);
        _bSceneSaveRowsDirty = true;
    }

    if (_bSceneSaveRowsDirty && _sceneSaveMountList && _sceneSaveEntryList &&
        _sceneSaveMountList->isAttached() && _sceneSaveEntryList->isAttached()) {
        rebuildSceneSaveRows();
        _bSceneSaveRowsDirty = false;
    }

    const Extent2D logicalExtent = _tree->getLogicalExtent();
    const glm::vec2 extent = {static_cast<float>(logicalExtent.width), static_cast<float>(logicalExtent.height)};
    const glm::vec2 desired = _sceneSavePanel->computeDesiredSize();
    _sceneSaveOverlay->_contentPos = {
        std::max(0.0f, (extent.x - desired.x) * 0.5f),
        std::max(0.0f, (extent.y - desired.y) * 0.5f),
    };

    const std::filesystem::path targetDir = !_sceneSaveExplorer->getSelectedPath().empty()
                                                ? _sceneSaveExplorer->getSelectedPath()
                                                : _sceneSaveExplorer->getCurrentDirectory();
    if (_sceneSavePathText) {
        std::string pathText = targetDir.string();
        if (const FileExplorer::MountPoint* active = _sceneSaveExplorer->getActiveMountPoint()) {
            pathText = active->name + ": " + pathText;
        }
        _sceneSavePathText->setText(pathText);
    }

    const std::string sceneName = _sceneSaveNameField ? _sceneSaveNameField->getText() : std::string{};
    const bool bCanSave = !sceneName.empty() && !targetDir.empty();
    if (_sceneSaveSaveButton) {
        _sceneSaveSaveButton->setEnabled(bCanSave);
    }
    if (_sceneSavePreviewText) {
        if (bCanSave) {
            _sceneSavePreviewText->setStyleKey("text.muted");
            _sceneSavePreviewText->setText(std::format("Will save to: {}", (targetDir / (sceneName + ".scene.json")).string()));
        }
        else {
            _sceneSavePreviewText->setStyleKey("text.error");
            _sceneSavePreviewText->setText("Enter a scene name and choose a directory.");
        }
    }
}

void EditorSurface::rebuildSceneSaveRows()
{
    if (!_sceneSaveExplorer || !_tree || !_sceneSaveMountList || !_sceneSaveEntryList) {
        return;
    }

    const FileExplorer::MountPoint* active = _sceneSaveExplorer->getActiveMountPoint();
    const std::filesystem::path selectedPath = _sceneSaveExplorer->getSelectedPath();
    std::vector<FileExplorer::FEntry> entries;
    _sceneSaveExplorer->collectEntries(entries);

    if (!_sceneSaveMountReconciler) {
        _sceneSaveMountReconciler = std::make_unique<UIKeyedChildReconciler>(
            *_tree, *_sceneSaveMountList, makeContentRowFactory());
    }
    if (!_sceneSaveEntryReconciler) {
        _sceneSaveEntryReconciler = std::make_unique<UIKeyedChildReconciler>(
            *_tree, *_sceneSaveEntryList, makeContentRowFactory());
    }

    std::vector<std::string> mountKeys;
    mountKeys.reserve(_sceneSaveExplorer->getMountPoints().size());
    for (const auto& mp : _sceneSaveExplorer->getMountPoints()) {
        mountKeys.push_back("SceneSaveMount_" + mp.name);
    }
    _sceneSaveMountReconciler->reconcile(
        mountKeys,
        [this, active](UIElement& child, const std::string&, size_t index) {
            const auto& mp = _sceneSaveExplorer->getMountPoints()[index];
            updateContentRow(child,
                             mp.name,
                             mp.name,
                             active != nullptr && active->name == mp.name,
                             [this](const std::string& itemId) { selectSceneSaveMount(itemId); },
                             [this](const std::string& itemId) { selectSceneSaveMount(itemId); });
        },
        bindEditorListRowSlot);

    std::vector<std::string> entryKeys;
    entryKeys.reserve(entries.size());
    for (const auto& entry : entries) {
        entryKeys.push_back("SceneSaveEntry_" + entry.name);
    }
    _sceneSaveEntryReconciler->reconcile(
        entryKeys,
        [this, &entries, &selectedPath](UIElement& child, const std::string&, size_t index) {
            const auto& entry = entries[index];
            const std::filesystem::path path = entry.path;
            updateContentRow(child,
                             entry.name + "/",
                             entry.name,
                             selectedPath == path,
                             [this, path](const std::string&) {
                                 if (_sceneSaveExplorer) {
                                     _sceneSaveExplorer->setSelectedPath(path);
                                     _bSceneSaveRowsDirty = true;
                                 }
                             },
                             [this, path](const std::string&) { activateSceneSaveItem(path, true); });
        },
        bindEditorListRowSlot);
}

void EditorSurface::selectSceneSaveMount(const std::string& itemId)
{
    if (!_sceneSaveExplorer) {
        return;
    }
    for (const auto& candidate : _sceneSaveExplorer->getMountPoints()) {
        if (candidate.name == itemId) {
            _sceneSaveExplorer->selectMountPoint(candidate);
            _bSceneSaveRowsDirty = true;
            break;
        }
    }
}

void EditorSurface::activateSceneSaveItem(const std::filesystem::path& path, bool bIsDirectory)
{
    if (!_sceneSaveExplorer || !bIsDirectory) {
        return;
    }
    if (_sceneSaveExplorer->navigateInto(path)) {
        _bSceneSaveRowsDirty = true;
    }
}

void EditorSurface::confirmSceneSaveDialog()
{
    if (!_sceneSaveExplorer || !_sceneSaveNameField || !_layer) {
        return;
    }

    const std::string sceneName = _sceneSaveNameField->getText();
    const std::filesystem::path targetDir = !_sceneSaveExplorer->getSelectedPath().empty()
                                                ? _sceneSaveExplorer->getSelectedPath()
                                                : _sceneSaveExplorer->getCurrentDirectory();
    if (sceneName.empty() || targetDir.empty()) {
        return;
    }

    const std::string targetDirUtf8 = path_utils::pathToUtf8String(targetDir);
    ConfigManager::Editor("editor").set("sceneSaveDialog.lastDirectory", targetDirUtf8).flush();
    const std::string scenePath = targetDirUtf8 + "/" + sceneName + ".scene.json";
    _layer->setCurrentScenePath(scenePath);
    if (Scene* scene = _layer->getEditableScene()) {
        scene->setName(sceneName);
    }
    if (App* app = App::get()) {
        app->getSceneServices().saveScene(scenePath);
    }
    YA_CORE_INFO("Scene saved to: {}", scenePath);

    if (_sceneSaveOverlay) {
        _sceneSaveOverlay->close();
    }
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
    return dynamic_cast<UITextField*>(focused) != nullptr ||
           (_inspectorTab && _inspectorTab->wantsTextInput(*_tree));
}

bool EditorSurface::shouldRenderViewportGizmo() const
{
    return _layer && _layer->isProjectLoaded() && !_layer->isViewportMode2D();
}

void EditorSurface::presentViewportGizmo(ICommandBuffer& commandBuffer)
{
    if (!shouldRenderViewportGizmo() || !_viewportGizmoOverlay || !_layer) {
        return;
    }

    GuiSystem::get().beginFrame();
    _viewportGizmoOverlay->syncImGuiIO();
    _layer->renderViewportGizmoOverlay();
    GuiSystem::get().endFrame();
    (void)GuiSystem::get().render();
    GuiSystem::get().submit(commandBuffer);
}

} // namespace ya
