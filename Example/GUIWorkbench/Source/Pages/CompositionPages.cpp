#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "Core/Log.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/DockSpace/DockContext.h"
#include "GUI/Widgets/Controls/DockSpace/DockFloatingHost.h"
#include "GUI/Widgets/Controls/DockSpace/DockSpace.h"
#include "GUI/Widgets/WidgetTree.h"

namespace guiworkbench
{

void buildDockDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log)
{
    if (state.dockFloatingHost && state.dockFloatingHost->getTree() == &tree) {
        tree.detach(*state.dockFloatingHost);
    }
    state.dockFloatingHost.reset();

    auto dockContext = std::make_shared<ya::FDockContext>();
    dockContext->bAllowFloating = true;
    dockContext->bAllowTearOff  = true;

    auto dock = std::make_shared<ya::UIDockSpace>("DemoDock");
    dock->setContext(dockContext);

    auto page = ya::ui::column("DockDemo");
    page.child(dock, ya::ui::boxSlot().fill());
    ya::ui::build(tree, parent, std::move(page), ya::ui::canvasSlot().fill());

    auto floatHost = std::make_shared<ya::UIDockFloatingHost>("DemoFloatingHost");
    floatHost->bindContext(dockContext);
    ya::FCanvasSlotArgs hostFill;
    hostFill.anchorMin = {0.0f, 0.0f};
    hostFill.anchorMax = {1.0f, 1.0f};
    const ya::WidgetAttachment floatingAttached =
        tree.attachToLayer(ya::WidgetTree::ELayer::Popup, floatHost, hostFill);
    YA_CORE_ASSERT(floatingAttached.valid(), "Dock floating host attach failed");
    state.dockFloatingHost = floatHost;

    const auto makePanel = [](const std::string& name, const std::string& text)
    {
        return ya::ui::panel(name + "_Body")
            .setStyleKey("panel.canvas")
            .child(ya::ui::text(name + "_Label")
                       .setText(text)
                       .setFontSize(14)
                       .setHAlign(ya::EWidgetAlignH::Center)
                       .setVAlign(ya::EWidgetAlignV::Center),
                   ya::ui::canvasSlot().fill().offset({12.0f, 12.0f}))
            .release();
    };

    const ya::DockPanelId sceneId     = dockContext->addPanel("Scene", makePanel("Scene", "Scene viewport"));
    const ya::DockPanelId hierarchyId = dockContext->addPanel("Hierarchy", makePanel("Hierarchy", "Actor hierarchy"));
    const ya::DockPanelId inspectorId = dockContext->addPanel("Inspector", makePanel("Inspector", "Inspector panel"));
    const ya::DockPanelId consoleId   = dockContext->addPanel("Console", makePanel("Console", "Console output"));
    const ya::DockPanelId assetsId    = dockContext->addPanel("Assets", makePanel("Assets", "Asset browser"));
    dockContext->setPanelClosable(sceneId, false);

    auto&                model    = dockContext->dockModel();
    const ya::DockNodeId rootLeaf = model.getRootNode()->id;
    model.selectPanel(sceneId);
    model.splitLeaf(rootLeaf, ya::EDockCardinalSide::East, inspectorId, 0.74f);
    if (ya::FDockNode* sceneLeaf = model.findLeafForPanel(sceneId)) {
        model.splitLeaf(sceneLeaf->id, ya::EDockCardinalSide::West, hierarchyId, 0.28f);
    }
    if (ya::FDockNode* sceneLeaf = model.findLeafForPanel(sceneId)) {
        model.splitLeaf(sceneLeaf->id, ya::EDockCardinalSide::South, consoleId, 0.70f);
    }
    if (ya::FDockNode* hierarchyLeaf = model.findLeafForPanel(hierarchyId)) {
        model.movePanel(assetsId, hierarchyLeaf->id);
        model.selectPanel(hierarchyId);
    }
    dockContext->fireDockUpdated();
    log("Dock demo: drag tabs to split / merge, drag out to float, drag floating title to re-dock");
}

} // namespace guiworkbench
