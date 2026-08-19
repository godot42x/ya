// GUIWorkbench — the retain-mode GUI feature gallery.
//
// Every page below is one gallery section exercising a framework capability
// end-to-end (not just unit tests). Pages own their scenario files under
// Example/GUIWorkbench/Scenarios/ for pre-manual acceptance:
//
//   Render       correctness baseline (text/image/clip/first-frame)
//   Widgets      basic controls (button/checkbox/slider/combo/field)
//   Layout       box layout (H/V, spacing, padding, alignment, anchors)
//   Menus        popup menus + the shell menu bar
//   DragDrop     drag sessions (sources onto targets)
//   Modal        popup dialog over a transparent shield
//   ScrollSplit  scroll viewport + split pane
//   Gallery      reactive bindings, style system, tree view, vector
//                primitives, table grid, input extras, drag wrappers
//   Interactions tooltip, wrapped text, subtree disable, modal dialog
//   Editor       built-in workspace (selectable rows, split, inspector)
//
// New framework features go on a dedicated page (never appended to an
// existing one — inserting content shifts every scenario coordinate).
#include "GUIWorkbench.h"

#include "Core/KeyCode.h"
#include "Core/Log.h"

#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/TabBar.h"

#include <format>

namespace guiworkbench
{

void FWorkbenchApp::buildUI(ya::WidgetTree& tree)
{
    _tree = &tree;
    surface.setSmokeActionsEnabled(bSmokeActions);

    // Demo pages are example content: register them into the shell. The
    // builders capture this app's demo state; the shell stays demo-agnostic.
    surface.addPage("Render", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildRenderDemo(t, p, demoState, status);
    });
    surface.addPage("Widgets", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildWidgetsDemo(t, p, demoState, status);
    });
    surface.addPage("Layout", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildLayoutDemo(t, p, demoState, status);
    });
    surface.addPage("Menus", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildMenusDemo(t, p, demoState, status);
    });
    surface.addPage("DragDrop", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildDragDropDemo(t, p, demoState, status);
    });
    surface.addPage("Modal", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        demoState.resetHandles();
        buildModalDemo(t, p, demoState, status);
    });
    surface.addPage("ScrollSplit", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildScrollSplitDemo(t, p, demoState, status);
    });
    surface.addPage("Gallery", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildGalleryDemo(t, p, demoState, status);
    });
    surface.addPage("Interactions", [this](ya::WidgetTree& t, ya::UIElement& p, const std::function<void(const std::string&)>& status)
    {
        buildInteractionsDemo(t, p, demoState, status);
    });

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
    surface.updateUI();
}

void FWorkbenchApp::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
    surface.onRoutedEvent(event, result);
}

void FWorkbenchApp::dispatchPointer(const ya::Event& event, const glm::vec2& point)
{
    ya::WidgetEventContext ctx;
    ctx.logicalPoint = point;
    _tree->dispatchEvent(event, ctx);
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

    switch (frame) {
    case 3: {
        if (surface.getCurrentPageIndex() != 0) {
            surface.failSmoke("Demo automation: render page not selected");
        }
        click(demoState.renderProbeButton.get());
        if (demoState.renderProbeClicks != 1) {
            surface.failSmoke(std::format("Demo automation: render probe click failed (count={})", demoState.renderProbeClicks));
        }
        return true;
    }
    case 4: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[1].get()); // Widgets tab
        if (surface.getCurrentPageIndex() != 1) {
            surface.failSmoke("Demo automation: tab switch to Widgets failed");
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
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[2].get()); // Layout tab
        if (surface.getCurrentPageIndex() != 2) {
            surface.failSmoke("Demo automation: tab switch to Layout failed");
        }
        return true;
    }
    case 13: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[3].get()); // Menus tab
        if (surface.getCurrentPageIndex() != 3) {
            surface.failSmoke("Demo automation: tab switch to Menus failed");
        }
        return true;
    }
    case 14: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[4].get()); // DragDrop tab
        if (surface.getCurrentPageIndex() != 4) {
            surface.failSmoke("Demo automation: tab switch to DragDrop failed");
        }
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
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[5].get()); // Modal tab
        if (surface.getCurrentPageIndex() != 5) {
            surface.failSmoke("Demo automation: tab switch to Modal failed");
        }
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
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[6].get()); // ScrollSplit tab
        if (surface.getCurrentPageIndex() != 6) {
            surface.failSmoke("Demo automation: tab switch to ScrollSplit failed");
        }
        return true;
    }
    case 20: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[7].get()); // Editor tab
        if (surface.getCurrentPageIndex() != surface.getEditorPageIndex()) {
            surface.failSmoke("Demo automation: tab switch to Editor failed");
        }
        return true;
    }
    default:
        return false; // later frames: the shell's built-in Editor automation
    }
}

} // namespace guiworkbench
