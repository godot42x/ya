#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>

namespace guiworkbench
{

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("RenderDemo");
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);
    ya::ui::attachSlot(parent, *panel, ya::ui::canvasSlot().fill());

    auto form = std::make_shared<ya::UIContainer>("RenderForm");
    form->setPadding({16.0f, 12.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);
    ya::ui::attachSlot(*panel, *form, ya::ui::overlaySlot().fill());

    tree.attach(*form, makeLabel("Render — correctness baseline (text, button, image, edge markers)"));
    tree.attach(*form, makeBodyText("Use this page as the first-frame render sanity target before deeper layout/event refactors."));

    auto markerRow = std::make_shared<ya::UIContainer>("RenderMarkers");
    markerRow->setDirection(ya::EWidgetBoxLayout::Horizontal);
    markerRow->setSpacing(8.0f);
    tree.attach(*form, markerRow);
    ya::ui::attachSlot(*form, *markerRow, ya::ui::boxSlot().preferredSize({0.0f, 84.0f}));
    const auto addMarker = [&](const std::string& name, const std::string& label, const glm::vec4& color)
    {
        auto cell = std::make_shared<ya::UIPanel>(name);
        cell->setColor(color);
        tree.attach(*markerRow, cell);
        ya::ui::attachSlot(*markerRow, *cell, ya::ui::boxSlot().preferredSize({180.0f, 84.0f}));

        auto text    = makeBodyText(label);
        text->_hAlign = ya::EWidgetAlignH::Center;
        text->_vAlign = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    };

    addMarker("TopLeftMarker", "Top-left", {0.37f, 0.18f, 0.18f, 1.0f});
    addMarker("CenterMarker", "Center", {0.18f, 0.33f, 0.24f, 1.0f});
    addMarker("BottomRightMarker", "Bottom-right", {0.18f, 0.25f, 0.38f, 1.0f});

    auto imageRow = makeRow(tree, *form);
    tree.attach(*imageRow, makeBodyText("Image placeholder"));
    auto image         = std::make_shared<ya::UIImage>("RenderProbeImage");
    image->_assetPath  = "builtin/checkerboard";
    tree.attach(*imageRow, image);
    ya::ui::attachSlot(*imageRow, *image, ya::ui::boxSlot().preferredSize({128.0f, 96.0f}));

    state.renderProbeButton           = makeDemoButton("RenderProbe", "Render Probe", 160.0f);
    state.renderProbeButton->_onClick = [&state, log]
    {
        ++state.renderProbeClicks;
        state.renderLog = std::format("Render probe clicked ({})", state.renderProbeClicks);
        log(state.renderLog);
    };
    tree.attach(*form, state.renderProbeButton);
    ya::ui::attachSlot(*form, *state.renderProbeButton, ya::ui::boxSlot().preferredSize({160.0f, 26.0f}));

    tree.attach(*form, makeBodyText("Expected: readable left-to-right text, stable clipping, no inversion, no flicker on resize."));
}

} // namespace guiworkbench
