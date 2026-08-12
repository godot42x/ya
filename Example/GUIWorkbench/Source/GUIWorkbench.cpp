#include "GUIWorkbench.h"

namespace guiworkbench
{

void FWorkbenchApp::buildUI(ya::WidgetTree& tree)
{
    surface.setSmokeActionsEnabled(bSmokeActions);
    surface.buildUI(tree);
}

void FWorkbenchApp::updateUI()
{
    surface.updateUI();
}

void FWorkbenchApp::onRoutedEvent(const ya::Event& event, ya::EWidgetRouteResult result)
{
    surface.onRoutedEvent(event, result);
}

} // namespace guiworkbench
