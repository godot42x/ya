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

    surface.buildUI(tree);
    surface.externalAutomationStep = [this](int frame) { return runDemoAutomation(frame); };
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
        dispatchPointer(ya::MouseButtonPressedEvent(0), center);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), center);
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
        click(demoState.counterButton.get());
        if (demoState.clickCount != 1) {
            surface.failSmoke(std::format("Demo automation: counter click failed (count={})", demoState.clickCount));
        }
        return true;
    }
    case 4: {
        const auto& rect = demoState.slider->_layoutRect;
        dispatchPointer(ya::MouseButtonPressedEvent(0), {rect.pos.x + rect.extent.x * 0.8f, rect.pos.y + rect.extent.y * 0.5f});
        dispatchPointer(ya::MouseButtonReleasedEvent(0), {rect.pos.x + rect.extent.x * 0.8f, rect.pos.y + rect.extent.y * 0.5f});
        if (demoState.sliderValue < 0.5f) {
            surface.failSmoke(std::format("Demo automation: slider failed (value={:.2f})", demoState.sliderValue));
        }
        return true;
    }
    case 5: {
        click(demoState.checkA.get());
        if (demoState.bCheckA) {
            surface.failSmoke("Demo automation: checkbox toggle failed");
        }
        return true;
    }
    case 6: {
        click(demoState.combo.get());
        if (!demoState.combo->getTree()) {
            surface.failSmoke("Demo automation: combo open failed");
        }
        return true;
    }
    case 7: {
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Enter);
        if (demoState.comboIndex != 1) {
            surface.failSmoke(std::format("Demo automation: combo selection failed (index={})", demoState.comboIndex));
        }
        return true;
    }
    case 8: {
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
    case 9: {
        pressKey(ya::EKey::Down);
        pressKey(ya::EKey::Enter);
        if (surface.getStatusText() != "Menu: New Document") {
            surface.failSmoke(std::format("Demo automation: menu action failed ('{}')", surface.getStatusText()));
        }
        return true;
    }
    case 10: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[1].get()); // Layout tab
        if (surface.getCurrentPageIndex() != 1) {
            surface.failSmoke("Demo automation: tab switch to Layout failed");
        }
        return true;
    }
    case 11: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[2].get()); // Menus tab
        if (surface.getCurrentPageIndex() != 2) {
            surface.failSmoke("Demo automation: tab switch to Menus failed");
        }
        return true;
    }
    case 12: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[3].get()); // DragDrop tab
        if (surface.getCurrentPageIndex() != 3) {
            surface.failSmoke("Demo automation: tab switch to DragDrop failed");
        }
        return true;
    }
    case 13: {
        const glm::vec2 itemCenter = centerOf(demoState.dragItem.get());
        const glm::vec2 zoneCenter = centerOf(demoState.dropZone.get());
        dispatchPointer(ya::MouseButtonPressedEvent(0), itemCenter);
        dispatchPointer(ya::MouseMoveEvent(zoneCenter.x, zoneCenter.y), zoneCenter);
        dispatchPointer(ya::MouseButtonReleasedEvent(0), zoneCenter);
        if (demoState.dropLog.empty()) {
            surface.failSmoke("Demo automation: drag & drop failed");
        }
        return true;
    }
    case 14: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[4].get()); // Modal tab
        if (surface.getCurrentPageIndex() != 4) {
            surface.failSmoke("Demo automation: tab switch to Modal failed");
        }
        return true;
    }
    case 15: {
        click(demoState.openModalButton.get());
        if (!demoState.bModalOpen) {
            surface.failSmoke("Demo automation: modal open failed");
        }
        return true;
    }
    case 16: {
        pressKey(ya::EKey::Escape);
        if (demoState.bModalOpen) {
            surface.failSmoke("Demo automation: modal Esc close failed");
        }
        return true;
    }
    case 17: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[5].get()); // ScrollSplit tab
        if (surface.getCurrentPageIndex() != 5) {
            surface.failSmoke("Demo automation: tab switch to ScrollSplit failed");
        }
        return true;
    }
    case 18: {
        const auto& tabs = surface.getTabBar()->getChildren();
        click(tabs[6].get()); // Editor tab
        if (surface.getCurrentPageIndex() != surface.getEditorPageIndex()) {
            surface.failSmoke("Demo automation: tab switch to Editor failed");
        }
        return true;
    }
    default:
        return false; // frames >= 19: the shell's built-in Editor automation
    }
}

} // namespace guiworkbench
