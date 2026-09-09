// GUIWorkbench — Feature Gallery for the retain-mode GUI framework.
// Pages are grouped in the left rail (Diagnostics / Controls / Layout / ...).
// Scenario files live under Example/GUIWorkbench/Scenarios/.
#include "GUIWorkbench.h"
#include "Pages/DemoPageCommon.h"

#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Tooling/Workbench/WorkbenchTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"

#include <format>
#include <cstddef>
#include <string>

namespace guiworkbench
{

namespace
{

ya::UIElement* findNamed(ya::UIElement* node, const char* name)
{
    if (!node || !name) {
        return nullptr;
    }
    if (node->_name == name) {
        return node;
    }
    for (const auto& child : node->getChildren()) {
        if (ya::UIElement* hit = findNamed(child.get(), name)) {
            return hit;
        }
    }
    return nullptr;
}

ya::UIElement* findNamed(ya::WidgetTree& tree, const char* name)
{
    using ELayer = ya::WidgetTree::ELayer;
    for (ELayer layer : {ELayer::Content, ELayer::Popup, ELayer::Tooltip, ELayer::DragIme}) {
        if (ya::UIElement* hit = findNamed(tree.getLayer(layer), name)) {
            return hit;
        }
    }
    return nullptr;
}

} // namespace

void ExtraOsWindowDemo::buildUI(ya::WidgetTree& tree)
{
    auto* content = tree.getLayer(ya::WidgetTree::ELayer::Content);
    if (!content) {
        return;
    }
    auto drop = makeDemoDropTarget(
        "extra-drop",
        "Drop here (from Windows page)",
        {},
        [this](const std::string& payload)
        {
            dropLabel->set(std::format("Dropped '{}'", payload));
        });
    auto page = ya::ui::column("extra-root")
                    .setDirection(ya::EWidgetBoxLayout::Vertical)
                    .setPadding(glm::vec2(16.0f))
                    .setSpacing(8.0f)
                    .child(ya::ui::text("extra-title").setText(title).setFontSize(18))
                    .child(ya::ui::text("extra-body")
                               .setText("Independent WidgetTree on the shared device. Close me; the gallery stays."))
                    .child(ya::ui::button("extra-click")
                               .child(ya::ui::text("extra-click-label").bindText(clickLabel))
                               .setOnClick([this]
                               {
                                   ++clicks;
                                   clickLabel->set("Clicked: " + std::to_string(clicks));
                               }))
                    .child(ya::ui::text("extra-drop-log").bindText(dropLabel).setFontSize(13))
                    .child(std::move(drop), ya::ui::boxSlot().preferredSize({0.0f, 80.0f}));
    ya::ui::build(tree, *content, std::move(page), ya::ui::canvasSlot().fill());
}

void FWorkbenchApp::pruneClosedExtras()
{
    if (!_guiApp) {
        return;
    }
    size_t i = 0;
    while (i < _extraIds.size()) {
        if (_guiApp->findTree(_extraIds[i]) == nullptr) {
            _extraIds.erase(_extraIds.begin() + static_cast<std::ptrdiff_t>(i));
            _extraDemos.erase(_extraDemos.begin() + static_cast<std::ptrdiff_t>(i));
        }
        else {
            ++i;
        }
    }
    _extraCountLabel->set(std::format("Open extras: {}", _extraIds.size()));
}

void FWorkbenchApp::openExtraWindow()
{
    if (!_guiApp) {
        YA_CORE_ERROR("GUIWorkbench: openExtraWindow before bindHost");
        return;
    }
    pruneClosedExtras();
    auto demo   = std::make_unique<ExtraOsWindowDemo>();
    demo->title = std::format("Extra {}", _extraDemos.size() + 1);
    ya::FGUIWindowHostConfig config;
    config.title         = demo->title;
    config.width         = 480;
    config.height        = 360;
    config.bEscapeQuits  = true;
    const ya::GUIWindowId id = _guiApp->openWindow(config, *demo);
    if (id == 0) {
        YA_CORE_ERROR("GUIWorkbench: failed to open extra OS window");
        return;
    }
    _extraIds.push_back(id);
    _extraDemos.push_back(std::move(demo));
    if (ya::WidgetTree* extraTree = _guiApp->findTree(id); extraTree && _tree) {
        extraTree->setTheme(_tree->getTheme());
    }
    _extraCountLabel->set(std::format("Open extras: {}", _extraIds.size()));
}

void FWorkbenchApp::closeLatestExtra()
{
    if (!_guiApp) {
        return;
    }
    pruneClosedExtras();
    if (_extraIds.empty()) {
        return;
    }
    _guiApp->closeWindow(_extraIds.back());
}

void FWorkbenchApp::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;
    surface.setSmokeActionsEnabled(bSmokeActions);

    // Tree-level theme (style-system Phase 2/3): the WorkbenchTheme (Phase 4)
    // bakes the workbench token palette into typed styles for every canonical
    // key. setTheme swaps dark/light; every themed widget repaints through
    // the generation token. Theme CONTENT is app-owned; the framework owns
    // the resolve + invalidation mechanism.
    _darkTheme  = buildWorkbenchTheme(/*bDark=*/true);
    _lightTheme = buildWorkbenchTheme(/*bDark=*/false);
    tree.setTheme(_darkTheme.get());

    surface.onToggleTheme = [this](bool bDark)
    {
        _bDarkTheme = bDark;
        if (_tree) {
            _tree->setTheme((bDark ? _darkTheme : _lightTheme).get());
        }
    };
    surface.bDarkTheme = true;

    // Demo pages are example content: register them into the shell. The
    // builders capture this app's demo state; the shell stays demo-agnostic.
    surface.addPage("Diagnostics", "Render", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildRenderDemo(t, p, demoState, status);
    });
    surface.addPage("Controls", "Widgets", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildWidgetsDemo(t, p, demoState, status);
    });
    surface.addPage("Controls", "Inputs", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildInputsDemo(t, p, demoState, status);
    });
    surface.addPage("Layout", "Box", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildLayoutDemo(t, p, demoState, status);
    });
    surface.addPage("Layout", "Hosts", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildHostsDemo(t, p, demoState, status);
    });
    surface.addPage("Layout", "ScrollSplit", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildScrollSplitDemo(t, p, demoState, status);
    });
    surface.addPage("Paint", "Brush", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildBrushDemo(t, p, demoState, status);
    });
    surface.addPage("Text", "Text", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildTextDemo(t, p, demoState, status);
    });
    surface.addPage("Text", "Fonts", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildFontsDemo(t, p, demoState, status);
    });
    surface.addPage("Style", "Theme", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildThemeDemo(t, p, demoState, status, [this](bool bDark)
        {
            _bDarkTheme = bDark;
            _tree->setTheme((bDark ? _darkTheme : _lightTheme).get());
        });
    });
    surface.addPage("Overlays", "Menus", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildMenusDemo(t, p, demoState, status);
    });
    surface.addPage("Overlays", "Dialog", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildDialogDemo(t, p, demoState, status);
    });
    surface.addPage("Interaction", "DragDrop", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildDragDropDemo(t, p, demoState, status);
    });
    surface.addPage("Interaction", "Enable", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildEnableDemo(t, p, demoState, status);
    });
    surface.addPage("Data", "Binding", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildBindingDemo(t, p, demoState, status);
    });
    surface.addPage("Data", "Tree", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildTreeDemo(t, p, demoState, status);
    });
    surface.addPage("Data", "Table", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildTableDemo(t, p, demoState, status);
    });
    surface.addPage("Composition", "Dock", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildDockDemo(t, p, demoState, status);
    });
    surface.setPageLeave("Dock", [this](ya::WidgetTree& t)
    {
        if (demoState.dockFloatingHost && demoState.dockFloatingHost->isAttached()) {
            t.detach(*demoState.dockFloatingHost);
        }
        demoState.dockFloatingHost.reset();
    });
    surface.addPage("Composition", "Windows", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildWindowsDemo(t,
                         p,
                         status,
                         [this] { openExtraWindow(); },
                         [this] { closeLatestExtra(); },
                         _extraCountLabel);
    });

    // DSL page: typed builders materialize live widgets once. The press
    // counter is a Reactive binding, so clicks do not rebuild the tree.
    struct FDslPageModel
    {
        std::shared_ptr<ya::Reactive<std::string>> pressLabel =
            std::make_shared<ya::Reactive<std::string>>("Pressed: 0");
        int pressCount = 0;
    };
    auto dslModel = std::make_shared<FDslPageModel>();

    surface.addPage("Composition", "DSL", [dslModel](ya::WidgetTree& tree, ya::UIElement& parent, const std::function<void(const std::string&)>&)
    {
        auto page = ya::ui::column("dsl-root")
                        .setDirection(ya::EWidgetBoxLayout::Vertical)
                        .setPadding(glm::vec2(16.0f))
                        .setSpacing(8.0f)
                        .children(
                            ya::ui::text("dsl-title").setText("DSL Page (live construct)"),
                            ya::ui::text("dsl-sub").setText("Typed builders attach real widgets; values bind through Reactive."),
                            ya::ui::button("dsl-button")
                                .child(ya::ui::text("dsl-button-label").bindText(dslModel->pressLabel))
                                .setOnClick([dslModel]
                                {
                                    ++dslModel->pressCount;
                                    dslModel->pressLabel->set("Pressed: " + std::to_string(dslModel->pressCount));
                                }),
                            ya::ui::row("dsl-row")
                                .setSpacing(8.0f)
                                .children(
                                    ya::ui::text("dsl-cell-a").setText("cell A"),
                                    ya::ui::text("dsl-cell-b").setText("cell B")));
        ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());
    });
    // Register the built-in Editor page before applyStartPage so
    // `--start-page=Editor` resolves. Empty builder uses the surface demo.
    surface.addPage("Composition", "Editor", {});

    applyStartPage();

    surface.buildUI(tree);
    surface.externalAutomationStep = [this](int frame) { return runDemoAutomation(frame); };
}

void FWorkbenchApp::applyStartPage()
{
    if (startPageName.empty()) {
        return;
    }
    const int index = surface.findPageIndexByName(startPageName);
    if (index < 0) {
        YA_CORE_WARN("GUIWorkbench: unknown start page '{}'", startPageName);
        return;
    }
    surface.setInitialPageIndex(index);
}

void FWorkbenchApp::updateUI()
{
    pruneClosedExtras();
    surface.updateUI();
}

void FWorkbenchApp::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
    surface.onRoutedEvent(event, result);
}

void FWorkbenchApp::dispatchPointer(ya::WidgetTree& tree, const ya::Event& event, const glm::vec2& point)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = point;
    tree.dispatchEvent(event, ctx);
}

void FWorkbenchApp::dispatchPointer(const ya::Event& event, const glm::vec2& point)
{
    if (_tree) {
        dispatchPointer(*_tree, event, point);
    }
}

void FWorkbenchApp::dispatchKey(const ya::Event& event)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = {-1.0f, -1.0f};
    _tree->dispatchEvent(event, ctx);
}

bool FWorkbenchApp::runDemoAutomation(int frame)
{
    const auto centerOf = [](const ya::UIElement* element) -> glm::vec2
    {
        return element ? element->_layoutRect.pos + element->_layoutRect.extent * 0.5f : glm::vec2{};
    };
    const auto click = [this, &centerOf](const ya::UIElement* element)
    {
        const glm::vec2 center = centerOf(element);
        dispatchPointer(ya::MouseButtonPressedEvent(ya::EMouse::Left), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(ya::EMouse::Left), center);
    };
    const auto pressKey = [this](ya::EKey::T key)
    {
        ya::KeyPressedEvent event;
        event._keyCode = key;
        event._mod     = 0;
        dispatchKey(event);
    };

    const auto gotoPage = [this](const char* name) -> bool
    {
        if (!surface.selectPageByName(name)) {
            surface.failSmoke(std::format("Demo automation: unknown page '{}'", name));
            return false;
        }
        return true;
    };

    switch (frame) {
    case 3: {
        if (surface.findPageIndexByName("Render") != surface.getCurrentPageIndex()) {
            surface.failSmoke("Demo automation: render page not selected");
        }
        click(demoState.renderProbeButton.get());
        if (demoState.renderProbeClicks != 1) {
            surface.failSmoke(std::format("Demo automation: render probe click failed (count={})", demoState.renderProbeClicks));
        }
        return true;
    }
    case 4: {
        if (!gotoPage("Widgets")) {
            return true;
        }
        return true;
    }
    case 5: {
        click(demoState.counterButton.get());
        if (demoState.clickCount != 1) {
            surface.failSmoke(std::format("Demo automation: counter click failed (count={})", demoState.clickCount));
        }
        return true;
    }
    case 6: {
        const auto& rect = demoState.slider->_layoutRect;
        dispatchPointer(ya::MouseButtonPressedEvent(ya::EMouse::Left), {rect.pos.x + rect.extent.x * 0.8f, rect.pos.y + rect.extent.y * 0.5f});
        dispatchPointer(ya::MouseButtonReleasedEvent(ya::EMouse::Left), {rect.pos.x + rect.extent.x * 0.8f, rect.pos.y + rect.extent.y * 0.5f});
        if (demoState.sliderValue < 0.5f) {
            surface.failSmoke(std::format("Demo automation: slider failed (value={:.2f})", demoState.sliderValue));
        }
        return true;
    }
    case 7: {
        click(demoState.checkA.get());
        if (demoState.bCheckA) {
            surface.failSmoke("Demo automation: checkbox toggle failed");
        }
        return true;
    }
    case 8: {
        click(demoState.combo.get());
        if (!demoState.combo->getTree()) {
            surface.failSmoke("Demo automation: combo open failed");
        }
        return true;
    }
    case 9: {
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Enter);
        if (demoState.comboIndex != 1) {
            surface.failSmoke(std::format("Demo automation: combo selection failed (index={})", demoState.comboIndex));
        }
        return true;
    }
    case 10: {
        const auto& children = surface.getMenuBar()->getChildren();
        if (children.empty()) {
            surface.failSmoke("Demo automation: menu bar empty");
        }
        else {
            click(children[0].get());
            if (!surface.getMenuBar()->getOpenMenu()) {
                surface.failSmoke("Demo automation: menu open failed");
            }
        }
        return true;
    }
    case 11: {
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Enter);
        if (surface.getStatusText() != "Menu: New Document") {
            surface.failSmoke(std::format("Demo automation: menu action failed ('{}')", surface.getStatusText()));
        }
        return true;
    }
    case 12: {
        (void)gotoPage("Box");
        return true;
    }
    case 13: {
        (void)gotoPage("Menus");
        return true;
    }
    case 14: {
        (void)gotoPage("DragDrop");
        return true;
    }
    case 15: {
        const glm::vec2 itemCenter = centerOf(demoState.dragItem.get());
        const glm::vec2 zoneCenter = centerOf(demoState.dropZone.get());
        dispatchPointer(ya::MouseButtonPressedEvent(ya::EMouse::Left), itemCenter);
        dispatchPointer(ya::MouseMoveEvent(zoneCenter.x, zoneCenter.y), zoneCenter);
        dispatchPointer(ya::MouseButtonReleasedEvent(ya::EMouse::Left), zoneCenter);
        if (demoState.dropLog.empty()) {
            surface.failSmoke("Demo automation: drag & drop failed");
        }
        return true;
    }
    case 16: {
        (void)gotoPage("Dialog");
        return true;
    }
    case 17: {
        click(demoState.openModalButton.get());
        if (!demoState.bModalOpen) {
            surface.failSmoke("Demo automation: modal open failed");
        }
        return true;
    }
    case 18: {
        pressKey(ya::EKey::Escape);
        if (demoState.bModalOpen) {
            surface.failSmoke("Demo automation: modal Esc close failed");
        }
        return true;
    }
    case 19: {
        if (!gotoPage("Windows")) {
            return true;
        }
        return true;
    }
    case 20: {
        if (!_tree || !findNamed(*_tree, "WindowsDemo") || !findNamed(*_tree, "windows-open")) {
            surface.failSmoke("Demo automation: Windows page widgets missing");
            return true;
        }
        if (_guiApp) {
            const int galleryClicks = demoState.clickCount;
            click(findNamed(*_tree, "windows-open"));
            pruneClosedExtras();
            if (_guiApp->extraWindowCount() != 1u || _extraIds.size() != 1u || _extraDemos.size() != 1u) {
                surface.failSmoke(std::format("Demo automation: Open extra window failed (count={})",
                                              _guiApp->extraWindowCount()));
                return true;
            }
            ya::WidgetTree* extraTree = _guiApp->findTree(_extraIds.back());
            if (!extraTree) {
                surface.failSmoke("Demo automation: extra WidgetTree missing after Open");
                return true;
            }
            extraTree->tick(0.0f);
            extraTree->layout();
            ya::UIElement* extraClick = findNamed(*extraTree, "extra-click");
            if (!extraClick) {
                surface.failSmoke("Demo automation: extra-click missing");
                return true;
            }
            const glm::vec2 extraCenter = centerOf(extraClick);
            dispatchPointer(*extraTree, ya::MouseButtonPressedEvent(ya::EMouse::Left), extraCenter);
            dispatchPointer(*extraTree, ya::MouseButtonReleasedEvent(ya::EMouse::Left), extraCenter);
            if (_extraDemos.back()->clicks != 1) {
                surface.failSmoke(std::format("Demo automation: extra click failed (count={})",
                                              _extraDemos.back()->clicks));
                return true;
            }
            if (demoState.clickCount != galleryClicks) {
                surface.failSmoke("Demo automation: extra click leaked into gallery FDemoState");
                return true;
            }
            const ya::GUIWindowId extraId = _extraIds.back();
            _guiApp->onEvent(ya::WindowResizeEvent(extraId, 400, 300));
            extraTree = _guiApp->findTree(extraId);
            if (!extraTree || extraTree->getLogicalExtent().width != 400u ||
                extraTree->getLogicalExtent().height != 300u) {
                surface.failSmoke("Demo automation: extra resize failed");
                return true;
            }
            if (_tree->getLogicalExtent().width == 400u && _tree->getLogicalExtent().height == 300u) {
                surface.failSmoke("Demo automation: extra resize leaked into gallery tree");
                return true;
            }
            closeLatestExtra();
        }
        (void)gotoPage("Editor");
        return true;
    }
    default:
        return false; // later frames: the shell's built-in Editor automation
    }
}

} // namespace guiworkbench
