#include "GameEditor/UI/EditorSurface.h"

#include "Core/Event.h"
#include "Core/Config/ConfigManager.h"
#include "Core/Log.h"
#include "ECS/Entity.h"
#include "ECS/Component.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Tooling/Workbench/WorkbenchSurface.h"
#include "GameEditor/UI/EditorTheme.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
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
                   .fillParent()
                   .setPosition({12.0f, 12.0f})
                   .setSize({-24.0f, -24.0f})
                   .setHAlign(EWidgetAlignH::Center)
                   .setVAlign(EWidgetAlignV::Center))
        .release();
}

std::shared_ptr<UISelectableRow> makeContentRow(const std::string& key,
                                                const std::string& label,
                                                const std::string& itemId,
                                                std::function<void(const std::string&)> onSelect,
                                                std::function<void(const std::string&)> onActivate)
{
    auto row = std::make_shared<UISelectableRow>(key);
    row->_itemId = itemId;
    row->setSize({0.0f, 22.0f});
    row->_onSelect   = std::move(onSelect);
    row->_onActivate = std::move(onActivate);

    auto text = std::make_shared<UIText>(key + "_Label");
    text->_fontSize = 13;
    text->setSize({0.0f, 22.0f});
    text->setText(label);
    text->setPosition({10.0f, 0.0f});
    text->_vAlign = EWidgetAlignV::Center;
    row->addDetachedChild(text);
    return row;
}

ui::UIButtonWidgetBuilder labeledButton(std::string key, const std::string& label, float width, float height = 26.0f)
{
    std::string labelKey = key + "_Label";
    return ui::button(std::move(key))
        .setSize({width, height})
        .child(ui::text(std::move(labelKey))
                   .setText(label)
                   .setFontSize(13)
                   .fillParent()
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
    _nameField.reset();
    _inspectorEmpty.reset();
    _statsText.reset();
    _transformDrags = {};
    _contentExplorer.reset();
    _contentPathText.reset();
    _contentMountList.reset();
    _contentEntryList.reset();
    _contentFingerprint.clear();
    _bContentRowsDirty = true;
    _viewportTexture.reset();
    _viewportImageResource.reset();
    _viewportImageView.reset();
    _hierarchyFingerprint.clear();
    _inspectorBoundId.clear();
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
    _nameField.reset();
    _inspectorEmpty.reset();
    _statsText.reset();
    _transformDrags = {};
    _workbench.reset();
    _contentExplorer.reset();
    _contentPathText.reset();
    _contentMountList.reset();
    _contentEntryList.reset();
    _contentFingerprint.clear();
    _bContentRowsDirty = true;
    clearSceneSaveDialog();
    _hierarchyFingerprint.clear();
    _inspectorBoundId.clear();

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
                     .setStyleKey("text.muted")
                     .setSize({720.0f, 40.0f});

    auto refresh = labeledButton("RefreshProjects", "Refresh Projects", 160.0f)
                       .setOnClick([this]() { _layer->requestRefreshProjectBrowser(); });
    auto exitBtn = labeledButton("ExitEditor", "Exit Editor", 160.0f)
                       .setOnClick([]() {
                           if (auto* app = App::get()) {
                               app->requestQuit();
                           }
                       });

    auto list = std::make_shared<UITreeView>("ProjectList");
    list->setSize({720.0f, 320.0f});
    _hierarchyView = list;
    _hierarchyRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    list->bindData(_hierarchyRoots);
    list->_onSelectionChanged = [this](const std::string& id) {
        int index = 0;
        if (auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), index);
            ec == std::errc{} && ptr == id.data() + id.size()) {
            _layer->setProjectBrowserSelection(index);
        }
    };

    auto openBtn = labeledButton("OpenProject", "Open Project", 160.0f)
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
                    .fillParent()
                    .setPadding({48.0f, 48.0f})
                    .setSpacing(12.0f)
                    .children(std::move(title),
                              std::move(blurb),
                              ui::row("ProjectActions")
                                  .setSpacing(8.0f)
                                  .children(std::move(refresh), std::move(exitBtn)),
                              list,
                              std::move(openBtn),
                              std::move(errorText));
    ui::build(*_tree, *_tree->getLayer(WidgetTree::ELayer::Content), std::move(page));
}

void EditorSurface::buildEditorChrome(App& app)
{
    (void)app;
    _root = ui::panel("EditorRoot").fillParent().setStyleKey("panel.window").share();
    const WidgetAttachment attached = _tree->attachToLayer(WidgetTree::ELayer::Content, _root);
    YA_CORE_ASSERT(attached.valid(), "EditorSurface: failed to attach editor root");

    _menuBar = std::make_shared<UIMenuBar>("EditorMenu");
    _menuBar->_anchorMin = {0.0f, 0.0f};
    _menuBar->_anchorMax = {1.0f, 0.0f};
    _menuBar->setPosition({0.0f, 0.0f});
    _menuBar->setSize({0.0f, kMenuHeight});
    _tree->attach(*_root, _menuBar);

    _menuBar->addItem("File", [this]() {
        return UIMenu::create({
            UIMenu::FItem{.label = "New Scene", .action = [this]() { _layer->cmdNewScene(); }},
            UIMenu::FItem{.label = "Save Scene", .action = [this]() { _layer->cmdSaveScene(); }},
            UIMenu::FItem{.label = "Save Scene As", .action = [this]() { openSceneSaveDialog(); }},
            UIMenu::FItem::Separator(),
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

    auto play = labeledButton("Play", "Play", 72.0f, 28.0f).setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startRuntime(); });
        }
    });
    auto simulate = labeledButton("Simulate", "Simulate", 88.0f, 28.0f).setOnClick([]() {
        if (auto* app = App::get()) {
            app->getTaskManager().registerFrameTask([app]() { app->startSimulation(); });
        }
    });
    auto stop = labeledButton("Stop", "Stop", 72.0f, 28.0f).setOnClick([]() {
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
    auto mode3d = labeledButton("Mode3D", "3D", 44.0f, 28.0f).setOnClick([this]() {
        _layer->setViewportMode(EViewportMode::Mode3D);
    });
    auto mode2d = labeledButton("Mode2D", "2D", 44.0f, 28.0f).setOnClick([this]() {
        _layer->setViewportMode(EViewportMode::Mode2D);
    });
    auto modeText = ui::text("ToolbarMode").setFontSize(13).setText("EDIT").setSize({88.0f, 20.0f});
    _toolbarModeText = modeText.share();

    auto toolbar = ui::row("EditorToolbar")
                       .setSpacing(8.0f)
                       .setPadding({8.0f, 4.0f})
                       .children(std::move(play),
                                 std::move(simulate),
                                 std::move(stop),
                                 std::move(mode3d),
                                 std::move(mode2d),
                                 std::move(modeText));
    auto toolbarWidget = toolbar.share();
    toolbarWidget->_anchorMin = {0.0f, 0.0f};
    toolbarWidget->_anchorMax = {1.0f, 0.0f};
    toolbarWidget->setPosition({0.0f, kMenuHeight});
    toolbarWidget->setSize({0.0f, kToolbarHeight});
    ui::build(*_tree, *_root, std::move(toolbar));

    _dockWorkspace = std::make_shared<UIDockWorkspace>();
    _dockSpace     = std::make_shared<UIDockSpace>("EditorDock");
    _dockSpace->setWorkspace(_dockWorkspace);
    _dockSpace->_anchorMin = {0.0f, 0.0f};
    _dockSpace->_anchorMax = {1.0f, 1.0f};
    _dockSpace->setPosition({0.0f, kChromeTop});
    _dockSpace->setSize({0.0f, -kChromeTop});
    _tree->attach(*_root, _dockSpace);

    auto viewportImage = ui::image("ViewportImage").fillParent();
    _viewportImage     = viewportImage.share();
    _viewportImage->_hitFilter = EWidgetHitFilter::Stop;
    auto viewportBody = ui::panel("ViewportBody")
                            .fillParent()
                            .setStyleKey("panel.canvas")
                            .child(std::move(viewportImage));

    auto hierarchyView = std::make_shared<UITreeView>("HierarchyTree");
    hierarchyView->_anchorMin = {0.0f, 0.0f};
    hierarchyView->_anchorMax = {1.0f, 1.0f};
    hierarchyView->setPosition({4.0f, 4.0f});
    hierarchyView->setSize({-8.0f, -8.0f});
    _hierarchyView  = hierarchyView;
    _hierarchyRoots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    hierarchyView->bindData(_hierarchyRoots);
    hierarchyView->_onSelectionChanged = [this](const std::string& id) {
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
    };
    auto hierarchyBody = ui::panel("HierarchyBody")
                             .fillParent()
                             .setStyleKey("panel.canvas")
                             .child(hierarchyView);

    auto nameField = ui::textField("InspectorName").setSize({220.0f, 26.0f}).setFontSize(14);
    _nameField     = nameField.share();
    _nameField->_onCommit = [this](const std::string& text) {
        if (Entity* entity = _layer->getSelectedEntity()) {
            if (Scene* scene = _layer->getHierarchyScene()) {
                if (Node* node = scene->getNodeByEntity(entity)) {
                    node->setName(text);
                }
            }
        }
    };

    auto empty = ui::text("InspectorEmpty")
                     .setText("No selection")
                     .setStyleKey("text.muted");
    _inspectorEmpty = empty.share();

    auto inspectorForm = ui::column("InspectorForm")
                             .fillParent()
                             .setPadding({10.0f, 8.0f})
                             .setSpacing(6.0f)
                             .child(ui::text("InspectorTitle").setText("INSPECTOR").setStyleKey("text.eyebrow"))
                             .child(ui::text("NameLabel").setText("Name").setFontSize(12))
                             .child(std::move(nameField))
                             .child(std::move(empty))
                             .child(ui::text("TransformLabel").setText("Transform").setFontSize(12));

    const char* kDragNames[9] = {
        "PosX", "PosY", "PosZ", "RotX", "RotY", "RotZ", "SclX", "SclY", "SclZ",
    };
    const float kSpeeds[9] = {0.1f, 0.1f, 0.1f, 0.5f, 0.5f, 0.5f, 0.01f, 0.01f, 0.01f};
    auto transformRow = ui::column("TransformRows").setSpacing(4.0f);
    for (int axisGroup = 0; axisGroup < 3; ++axisGroup) {
        auto row = ui::row(std::format("TransformRow{}", axisGroup)).setSpacing(4.0f);
        for (int axis = 0; axis < 3; ++axis) {
            const int index = axisGroup * 3 + axis;
            auto drag = std::make_shared<UIDragFloat>(kDragNames[index]);
            drag->setSize({72.0f, 22.0f});
            drag->_speed = kSpeeds[index];
            drag->_onValueChanged = [this, index](float value) {
                Entity* entity = _layer->getSelectedEntity();
                if (!entity) {
                    return;
                }
                auto* tc = entity->getComponent<TransformComponent>();
                if (!tc) {
                    return;
                }
                glm::vec3 pos = tc->getPosition();
                glm::vec3 rot = tc->getRotation();
                glm::vec3 scl = tc->getScale();
                if (index < 3) {
                    pos[index] = value;
                    tc->setPosition(pos);
                }
                else if (index < 6) {
                    rot[index - 3] = value;
                    tc->setRotation(rot);
                }
                else {
                    scl[index - 6] = value;
                    tc->setScale(scl);
                }
            };
            _transformDrags[static_cast<size_t>(index)] = drag;
            row.child(drag);
        }
        transformRow.child(std::move(row));
    }
    inspectorForm.child(std::move(transformRow));
    auto inspectorBody = ui::panel("InspectorBody")
                             .fillParent()
                             .setStyleKey("panel")
                             .child(std::move(inspectorForm));

    auto statsText = ui::text("FrameStatsBody")
                         .setText("Frame Stats")
                         .setFontSize(13)
                         .fillParent()
                         .setPosition({12.0f, 12.0f})
                         .setSize({-24.0f, -24.0f});
    _statsText = statsText.share();
    auto statsBody = ui::panel("FrameStatsPanel")
                         .fillParent()
                         .setStyleKey("panel.canvas")
                         .child(std::move(statsText));

    auto workbenchHost = ui::panel("WorkbenchHost").fillParent().setStyleKey("panel.window").share();
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
    const DockPanelId inspectorId = _dockWorkspace->addPanel("Inspector", inspectorBody.release());
    const DockPanelId contentId   = _dockWorkspace->addPanel("Content Browser", buildContentBrowser());
    const DockPanelId statsId     = _dockWorkspace->addPanel("Frame Stats", statsBody.release());
    const DockPanelId workbenchId = _dockWorkspace->addPanel("GUI Workbench", workbenchHost);
    const DockPanelId runtimeId   = _dockWorkspace->addPanel("Runtime Tools", makePlaceholderPanel("RuntimeTools", "Runtime Tools — pending"));
    const DockPanelId designerId  = _dockWorkspace->addPanel("UI Designer", makePlaceholderPanel("UIDesigner", "UI Designer — pending"));
    const DockPanelId assetsId    = _dockWorkspace->addPanel("Asset Inspector", makePlaceholderPanel("Assets", "Asset Inspector — pending"));

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

    auto root = std::make_shared<UIContainer>("ContentBrowserRoot");
    root->setDirection(EWidgetBoxLayout::Vertical);
    root->setSpacing(2.0f);
    root->setPadding({4.0f, 4.0f});

    // Header: back / path / search.
    auto back = std::make_shared<UIButton>("ContentBack");
    back->setSize({52.0f, 22.0f});
    {
        auto label = std::make_shared<UIText>("ContentBack_Label");
        label->setText("< Back");
        label->setFontSize(12);
        label->_hAlign = EWidgetAlignH::Center;
        label->_vAlign = EWidgetAlignV::Center;
        back->addDetachedChild(label);
    }
    back->_onClick = [this]() {
        if (_contentExplorer) {
            _contentExplorer->navigateBack();
        }
    };

    auto pathText = std::make_shared<UIText>("ContentPath");
    pathText->_fontSize = 12;
    pathText->_vAlign   = EWidgetAlignV::Center;
    _contentPathText    = pathText;

    auto search = std::make_shared<UITextField>("ContentSearch");
    search->setSize({140.0f, 22.0f});
    search->_onTextChanged = [this](const std::string& text) {
        if (_contentExplorer) {
            _contentExplorer->setSearchText(text);
        }
    };

    auto header = std::make_shared<UIContainer>("ContentHeader");
    header->setDirection(EWidgetBoxLayout::Horizontal);
    header->setSpacing(6.0f);
    header->setSize({0.0f, 26.0f});
    header->addDetachedChild(back);
    header->addDetachedChild(pathText);
    header->addDetachedChild(search);
    root->addDetachedChild(header);

    // Body: mount list (fixed width) + entry list (fill).
    auto body = std::make_shared<UIContainer>("ContentBody");
    body->setDirection(EWidgetBoxLayout::Horizontal);
    body->setSpacing(4.0f);

    _contentMountList = std::make_shared<UIContainer>("ContentMounts");
    _contentMountList->setDirection(EWidgetBoxLayout::Vertical);
    _contentMountList->setSpacing(2.0f);
    _contentMountList->setSize({180.0f, 0.0f});

    _contentEntryList = std::make_shared<UIContainer>("ContentEntries");
    _contentEntryList->setDirection(EWidgetBoxLayout::Vertical);
    _contentEntryList->setSpacing(2.0f);
    _contentEntryList->_anchorMin = {0.0f, 0.0f};
    _contentEntryList->_anchorMax = {1.0f, 1.0f};
    _contentEntryList->setSize({0.0f, 0.0f});

    // Scroll hosts keep long directory listings from overflowing and give
    // wheel navigation; each host owns exactly the one list container.
    auto mountScroll = std::make_shared<UIScrollViewport>("ContentMountScroll");
    mountScroll->setAxis(EScrollAxis::Vertical);
    mountScroll->setSize({180.0f, 0.0f});
    mountScroll->addDetachedChild(_contentMountList);

    auto entryScroll = std::make_shared<UIScrollViewport>("ContentEntryScroll");
    entryScroll->setAxis(EScrollAxis::Vertical);
    entryScroll->_anchorMin = {0.0f, 0.0f};
    entryScroll->_anchorMax = {1.0f, 1.0f};
    entryScroll->setSize({0.0f, 0.0f});
    entryScroll->addDetachedChild(_contentEntryList);

    body->addDetachedChild(mountScroll);
    body->addDetachedChild(entryScroll);
    root->addDetachedChild(body);
    return root;
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
    syncInspector();
    syncToolbar(app);
    syncContentBrowser();
    syncSceneSaveDialog();
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

void EditorSurface::syncInspector()
{
    Entity* entity = _layer ? _layer->getSelectedEntity() : nullptr;
    const std::string boundId = entity ? entityIdKey(
                                    entity->getComponent<IDComponent>() ? entity->getComponent<IDComponent>()->_id.value : 0)
                                : std::string{};
    const bool bHasSelection = entity != nullptr;
    if (_inspectorEmpty) {
        _inspectorEmpty->setVisibility(bHasSelection ? EWidgetVisibility::Hidden : EWidgetVisibility::Visible);
    }
    if (!bHasSelection) {
        _inspectorBoundId.clear();
        return;
    }

    UIElement* focused = _tree->getFocused();
    const bool bNameBusy = focused == _nameField.get();
    if (_nameField && !bNameBusy) {
        if (Scene* scene = _layer->getHierarchyScene()) {
            if (Node* node = scene->getNodeByEntity(entity)) {
                _nameField->setText(node->getName());
            }
        }
    }

    auto* tc = entity->getComponent<TransformComponent>();
    if (!tc) {
        return;
    }
    const float values[9] = {
        tc->getPosition().x, tc->getPosition().y, tc->getPosition().z,
        tc->getRotation().x, tc->getRotation().y, tc->getRotation().z,
        tc->getScale().x,    tc->getScale().y,    tc->getScale().z,
    };
    for (size_t i = 0; i < _transformDrags.size(); ++i) {
        if (!_transformDrags[i] || focused == _transformDrags[i].get()) {
            continue;
        }
        _transformDrags[i]->setValue(values[i]);
    }
    _inspectorBoundId = boundId;
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
    }
    if (_bContentRowsDirty && _contentMountList && _contentEntryList &&
        _contentMountList->isAttached() && _contentEntryList->isAttached()) {
        rebuildContentRows();
        _bContentRowsDirty = false;
    }
}

void EditorSurface::rebuildContentRows()
{
    if (!_contentExplorer || !_tree || !_contentMountList || !_contentEntryList) {
        return;
    }

    const FileExplorer::MountPoint* active = _contentExplorer->getActiveMountPoint();

    // Mount rows.
    auto mountChildren = _contentMountList->getChildrenInPaintOrder();
    for (UIElement* child : mountChildren) {
        if (child && child->isAttached()) {
            _tree->detach(*child);
        }
    }
    for (const auto& mp : _contentExplorer->getMountPoints()) {
        auto row = makeContentRow(
            "ContentMount_" + mp.name,
            mp.name,
            mp.name,
            /*onSelect=*/[this](const std::string& itemId) {
                if (_contentExplorer) {
                    for (const auto& candidate : _contentExplorer->getMountPoints()) {
                        if (candidate.name == itemId) {
                            _contentExplorer->selectMountPoint(candidate);
                            break;
                        }
                    }
                }
            },
            /*onActivate=*/[this](const std::string& itemId) {
                if (_contentExplorer) {
                    for (const auto& candidate : _contentExplorer->getMountPoints()) {
                        if (candidate.name == itemId) {
                            _contentExplorer->selectMountPoint(candidate);
                            break;
                        }
                    }
                }
            });
        row->setSelected(active != nullptr && active->name == mp.name);
        _tree->attach(*_contentMountList, row);
    }

    // Entry rows.
    auto entryChildren = _contentEntryList->getChildrenInPaintOrder();
    for (UIElement* child : entryChildren) {
        if (child && child->isAttached()) {
            _tree->detach(*child);
        }
    }
    std::vector<FileExplorer::FEntry> entries;
    _contentExplorer->collectEntries(entries);
    for (const auto& entry : entries) {
        const std::filesystem::path path = entry.path;
        const bool                  bDir = entry.bIsDirectory;
        auto row = makeContentRow(
            "ContentEntry_" + entry.name,
            entry.bIsDirectory ? entry.name + "/" : entry.name,
            entry.name,
            /*onSelect=*/[this](const std::string&) {},
            /*onActivate=*/[this, path, bDir](const std::string&) {
                activateContentItem(path, bDir);
            });
        _tree->attach(*_contentEntryList, row);
    }

    if (_contentPathText) {
        std::string pathText = _contentExplorer->getCurrentDirectory().string();
        if (active) {
            pathText = active->name + ": " + pathText;
        }
        _contentPathText->setText(pathText);
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

    _sceneSaveOverlay = std::make_shared<UIPopupOverlay>("SceneSaveOverlay");
    _sceneSaveOverlay->setRole(UIPopupOverlay::EOverlayRole::Modal);
    _sceneSaveOverlay->_onDismiss = [this]() { clearSceneSaveDialog(); };

    auto panel = std::make_shared<UIPanel>("SceneSavePanel");
    panel->setSize({720.0f, 520.0f});
    panel->setStyleKey("panel.window");
    _sceneSavePanel = panel;

    auto root = std::make_shared<UIContainer>("SceneSaveRoot");
    root->_anchorMin = {0.0f, 0.0f};
    root->_anchorMax = {1.0f, 1.0f};
    root->setSize({0.0f, 0.0f});
    root->setDirection(EWidgetBoxLayout::Vertical);
    root->setSpacing(8.0f);
    root->setPadding({12.0f, 12.0f});
    panel->addDetachedChild(root);

    auto title = std::make_shared<UIText>("SceneSaveTitle");
    title->setText("Save Scene");
    title->setStyleKey("text.header");
    title->setFontSize(14);
    root->addDetachedChild(title);

    auto nameRow = std::make_shared<UIContainer>("SceneSaveNameRow");
    nameRow->setDirection(EWidgetBoxLayout::Horizontal);
    nameRow->setSpacing(6.0f);
    nameRow->setStretchLastChild(true);
    nameRow->setSize({0.0f, 26.0f});

    auto nameLabel = std::make_shared<UIText>("SceneSaveNameLabel");
    nameLabel->setText("Scene Name");
    nameLabel->setFontSize(12);
    nameLabel->setSize({90.0f, 26.0f});
    nameLabel->_vAlign = EWidgetAlignV::Center;
    nameRow->addDetachedChild(nameLabel);

    auto nameField = std::make_shared<UITextField>("SceneSaveName");
    nameField->setSize({0.0f, 26.0f});
    nameField->setText(defaultName);
    _sceneSaveNameField = nameField;
    nameRow->addDetachedChild(nameField);
    root->addDetachedChild(nameRow);

    auto pathText = std::make_shared<UIText>("SceneSavePath");
    pathText->setFontSize(12);
    pathText->setStyleKey("text.muted");
    _sceneSavePathText = pathText;
    root->addDetachedChild(pathText);

    auto search = std::make_shared<UITextField>("SceneSaveSearch");
    search->setSize({0.0f, 24.0f});
    search->_onTextChanged = [this](const std::string& text) {
        if (_sceneSaveExplorer) {
            _sceneSaveExplorer->setSearchText(text);
            _bSceneSaveRowsDirty = true;
        }
    };
    root->addDetachedChild(search);

    auto body = std::make_shared<UIContainer>("SceneSaveBody");
    body->setDirection(EWidgetBoxLayout::Horizontal);
    body->setSpacing(6.0f);
    body->setStretchLastChild(true);
    body->setSize({0.0f, 360.0f});

    _sceneSaveMountList = std::make_shared<UIContainer>("SceneSaveMounts");
    _sceneSaveMountList->setDirection(EWidgetBoxLayout::Vertical);
    _sceneSaveMountList->setSpacing(2.0f);
    _sceneSaveMountList->setSize({180.0f, 0.0f});
    auto mountScroll = std::make_shared<UIScrollViewport>("SceneSaveMountScroll");
    mountScroll->setAxis(EScrollAxis::Vertical);
    mountScroll->setSize({180.0f, 0.0f});
    mountScroll->addDetachedChild(_sceneSaveMountList);

    _sceneSaveEntryList = std::make_shared<UIContainer>("SceneSaveEntries");
    _sceneSaveEntryList->setDirection(EWidgetBoxLayout::Vertical);
    _sceneSaveEntryList->setSpacing(2.0f);
    _sceneSaveEntryList->setSize({0.0f, 0.0f});
    auto entryScroll = std::make_shared<UIScrollViewport>("SceneSaveEntryScroll");
    entryScroll->setAxis(EScrollAxis::Vertical);
    entryScroll->_anchorMin = {0.0f, 0.0f};
    entryScroll->_anchorMax = {1.0f, 1.0f};
    entryScroll->setSize({0.0f, 0.0f});
    entryScroll->addDetachedChild(_sceneSaveEntryList);

    body->addDetachedChild(mountScroll);
    body->addDetachedChild(entryScroll);
    root->addDetachedChild(body);

    auto preview = std::make_shared<UIText>("SceneSavePreview");
    preview->setFontSize(12);
    _sceneSavePreviewText = preview;
    root->addDetachedChild(preview);

    auto actions = std::make_shared<UIContainer>("SceneSaveActions");
    actions->setDirection(EWidgetBoxLayout::Horizontal);
    actions->setSpacing(8.0f);
    actions->getBoxLayout().setMainAxisAlignment(EWidgetMainAxisAlignment::End);

    auto back = labeledButton("SceneSaveBack", "Back", 72.0f, 26.0f).share();
    back->_onClick = [this]() {
        if (_sceneSaveExplorer && _sceneSaveExplorer->navigateBack()) {
            _bSceneSaveRowsDirty = true;
        }
    };
    actions->addDetachedChild(back);

    auto save = labeledButton("SceneSaveConfirm", "Save", 84.0f, 26.0f).share();
    save->_onClick = [this]() { confirmSceneSaveDialog(); };
    _sceneSaveSaveButton = save;
    actions->addDetachedChild(save);

    auto cancel = labeledButton("SceneSaveCancel", "Cancel", 84.0f, 26.0f).share();
    cancel->_onClick = [this]() {
        if (_sceneSaveOverlay) {
            _sceneSaveOverlay->close();
        }
    };
    actions->addDetachedChild(cancel);
    root->addDetachedChild(actions);

    _sceneSaveOverlay->addDetachedChild(panel);
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

    for (UIElement* child : _sceneSaveMountList->getChildrenInPaintOrder()) {
        if (child && child->isAttached()) {
            _tree->detach(*child);
        }
    }
    for (const auto& mp : _sceneSaveExplorer->getMountPoints()) {
        auto row = makeContentRow(
            "SceneSaveMount_" + mp.name,
            mp.name,
            mp.name,
            [this](const std::string& itemId) {
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
            },
            [this](const std::string& itemId) {
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
            });
        row->setSelected(active != nullptr && active->name == mp.name);
        _tree->attach(*_sceneSaveMountList, row);
    }

    for (UIElement* child : _sceneSaveEntryList->getChildrenInPaintOrder()) {
        if (child && child->isAttached()) {
            _tree->detach(*child);
        }
    }
    std::vector<FileExplorer::FEntry> entries;
    _sceneSaveExplorer->collectEntries(entries);
    for (const auto& entry : entries) {
        const std::filesystem::path path = entry.path;
        auto row = makeContentRow(
            "SceneSaveEntry_" + entry.name,
            entry.name + "/",
            entry.name,
            [this, path](const std::string&) {
                if (_sceneSaveExplorer) {
                    _sceneSaveExplorer->setSelectedPath(path);
                    _bSceneSaveRowsDirty = true;
                }
            },
            [this, path](const std::string&) { activateSceneSaveItem(path, true); });
        row->setSelected(selectedPath == path);
        _tree->attach(*_sceneSaveEntryList, row);
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
           dynamic_cast<UIDragFloat*>(focused) != nullptr;
}

} // namespace ya
