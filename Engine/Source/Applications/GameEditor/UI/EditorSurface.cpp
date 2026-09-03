#include "GameEditor/UI/EditorSurface.h"

#include "Core/Event.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "ECS/ECSRegistry.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GameEditor/UI/EditorTabRegistry.h"
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
#include "GameEditor/EditorLayer.h"
#include "Core/System/PathUtils.h"
#include "GameRuntime/App.h"
#include "Hierarchy/Node.h"
#include "RHI/Core/Texture.h"
#include "RHI/Core/RenderTexture.h"
#include "RHI/Core/Swapchain.h"
#include "RHI/NativeWindow.h"
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
constexpr float kEditorListRowHeight = 22.0f;
constexpr float kEditorListRowSpacing = 2.0f;
constexpr size_t kEditorListOverscan = 2;

std::string entityIdKey(uint64_t uuid)
{
    return std::format("e:{}", uuid);
}

bool parseEntityIdKey(const std::string& id, uint64_t& outUuid)
{
    if (id.size() < 3 || id[0] != 'e' || id[1] != ':') {
        return false;
    }
    const std::string_view digits{id.data() + 2, id.size() - 2};
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), outUuid);
    return ec == std::errc{} && ptr == digits.data() + digits.size();
}

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
    out.id    = entityIdKey(uuid);
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

/// One file-browser row: UISelectableRow host + a UIText label in its content
/// slot. Shared by the Content Browser and the Save Scene dialog, which both
/// rebuild their rows from a FileExplorer fingerprint.
ui::UISelectableRowWidgetBuilder contentRow(const std::string& key,
                                            const std::string& label,
                                            const std::string& itemId,
                                            std::function<void(const std::string&)> onSelect,
                                            std::function<void(const std::string&)> onActivate)
{
    return ui::selectableRow(key)
        .setItemId(itemId)
        .setContentPadding(FMargin{10.0f, 0.0f, 0.0f, 0.0f})
        .setOnSelect(std::move(onSelect))
        .setOnActivate(std::move(onActivate))
        .child(ui::text(key + "_Label")
                   .setText(label)
                   .setFontSize(13)
                   .setVAlign(EWidgetAlignV::Center));
}

void updateContentRow(UIElement& child,
                      const std::string& label,
                      const std::string& itemId,
                      bool selected,
                      std::function<void(const std::string&)> onSelect,
                      std::function<void(const std::string&)> onActivate)
{
    auto* row = dynamic_cast<UISelectableRow*>(&child);
    if (!row) {
        YA_CORE_ERROR("EditorSurface: keyed content row '{}' is not a UISelectableRow", child._name);
        return;
    }
    row->_itemId = itemId;
    row->setSelected(selected);
    row->_onSelect = std::move(onSelect);
    row->_onActivate = std::move(onActivate);
    if (!row->getChildren().empty()) {
        if (auto* text = dynamic_cast<UIText*>(row->getChildren().front().get())) {
            text->setText(label);
        }
    }
}

UIKeyedChildReconciler::Factory makeContentRowFactory()
{
    return [](const std::string& key, size_t) {
        return contentRow(key, key, key, {}, {}).release();
    };
}

void bindEditorListRowSlot(UISlot& slot, const std::string&, size_t)
{
    if (auto* box = slot.as<UIBoxSlot>()) {
        box->setPreferredSize({0.0f, kEditorListRowHeight});
    }
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

} // namespace

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
    _hierarchyView.reset();
    _hierarchyRoots.reset();
    _inspectorTab.reset();
    _statsText.reset();
    _contentExplorer.reset();
    _contentPathText.reset();
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
}

void EditorSurface::rebuild(App& app)
{
    _root.reset();
    _menuBar.reset();
    _toolbarModeText.reset();
    _dockWorkspace.reset();
    _dockSpace.reset();
    _viewportImage.reset();
    _hierarchyView.reset();
    _hierarchyRoots.reset();
    _inspectorTab.reset();
    _statsText.reset();
    _workbench.reset();
    _tabRegistry = std::make_unique<EditorTabRegistry>();
    _contentExplorer.reset();
    _contentPathText.reset();
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
                    .setOnSelectionChanged([this](const std::string& id) {
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

void EditorSurface::buildEditorChrome(App& app)
{
    (void)app;
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
            UIMenu::FItem{.label = "New Scene", .action = [this]() { _layer->cmdNewScene(); }},
            UIMenu::FItem{.label = "Save Scene", .action = [this]() { _layer->cmdSaveScene(); }},
            UIMenu::FItem{.label = "Save Scene As", .action = [this]() { openSceneSaveDialog(); }},
            UIMenu::FItem::separator(),
            UIMenu::FItem{.label = "Exit", .action = []() {
                 if (auto* app = App::get()) {
                     app->requestQuit();
                 }
             }},
        });
    });
    _menuBar->addItem("View", [this]() {
        return UIMenu::create({
            UIMenu::FItem{.label = "Viewport 3D", .action = [this]() { _layer->setViewportMode(EViewportMode::Mode3D); }},
            UIMenu::FItem{.label = "Viewport 2D", .action = [this]() { _layer->setViewportMode(EViewportMode::Mode2D); }},
        });
    });

    auto play = labeledButton("Play", "Play").setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startRuntime(); });
        }
    });
    auto simulate = labeledButton("Simulate", "Simulate").setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startSimulation(); });
        }
    });
    auto stop = labeledButton("Stop", "Stop").setOnClick([]() {
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
    });
    auto mode3d = labeledButton("Mode3D", "3D").setOnClick([this]() {
        _layer->setViewportMode(EViewportMode::Mode3D);
    });
    auto mode2d = labeledButton("Mode2D", "2D").setOnClick([this]() {
        _layer->setViewportMode(EViewportMode::Mode2D);
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

    _hierarchyRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _hierarchyView  = ui::treeView("HierarchyTree")
                         .bindData(_hierarchyRoots)
                         .setOnSelectionChanged([this](const std::string& id) {
                             uint64_t uuid = 0;
                             std::string entryId;
                             if (parseEntityIdKey(id, uuid)) {
                                 if (Scene* scene = _layer->getHierarchyScene()) {
                                     _layer->setSelectedEntity(scene->getEntityByUUID(uuid));
                                 }
                             }
                             else if (parseWidgetEntryKey(id, entryId)) {
                                 _layer->setSelectedWidgetEntryId(entryId);
                             }
                         })
                         .share();
    auto hierarchyBody = ui::panel("HierarchyBody")
                             .setStyleKey("panel.canvas")
                             .child(_hierarchyView, ui::canvasSlot().anchor({0.0f, 0.0f}, {1.0f, 1.0f}).offset({4.0f, 4.0f}));

    _inspectorTab = std::make_unique<EditorInspectorTab>(*_layer);
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
        .build = [](EditorLayer&, WidgetTree&) { return makePlaceholderPanel("RuntimeTools", "Runtime Tools — pending"); },
    });
    _tabRegistry->registerTab({
        .id = "ui-designer",
        .title = "UI Designer",
        .build = [](EditorLayer&, WidgetTree&) { return makePlaceholderPanel("UIDesigner", "UI Designer — pending"); },
    });
    _tabRegistry->registerTab({
        .id = "asset-inspector",
        .title = "Asset Inspector",
        .build = [](EditorLayer&, WidgetTree&) { return makePlaceholderPanel("Assets", "Asset Inspector — pending"); },
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
                               }
                           });

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
    if (!display || !display->isValid() || !display->getImageView()) {
        _viewportImage->setTexture(nullptr);
        _viewportTexture.reset();
        _viewportImageResource.reset();
        _viewportImageView.reset();
        return;
    }

    auto sourceImage     = display->getImageShared();
    auto sourceImageView = display->getImageViewShared();
    if (!sourceImage || !sourceImageView) {
        _viewportImage->setTexture(nullptr);
        return;
    }
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
    if (const auto* active = _contentExplorer->getActiveMountPoint()) {
        fingerprint += active->name;
    }

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
}

void EditorSurface::rebuildContentRows()
{
    if (!_contentExplorer || !_tree || !_contentMountList || !_contentEntryRows) {
        return;
    }

    const FileExplorer::MountPoint* active = _contentExplorer->getActiveMountPoint();
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
        [this, &entries, window](UIElement& child, const std::string&, size_t index) {
            const size_t itemIndex = window.first + index;
            const auto& entry = entries[itemIndex];
            const std::filesystem::path path = entry.path;
            const bool bDir = entry.bIsDirectory;
            updateContentRow(child,
                             bDir ? entry.name + "/" : entry.name,
                             entry.name,
                             false,
                             [](const std::string&) {},
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

EWidgetRouteResult EditorSurface::dispatchEvent(const Event& event, const glm::vec2& windowPoint)
{
    if (!_tree) {
        return EWidgetRouteResult::NotHandled;
    }
    WidgetEventContext ctx;
    ctx.logicalPoint = windowPoint;
    return _tree->dispatchEvent(event, ctx);
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

} // namespace ya
