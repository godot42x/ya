#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/WidgetTree.h"

#include <format>

namespace guiworkbench
{

void buildRenderDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIBorder>("RenderDemo");
    panel->setColor(kPanelColor);
    ya::ui::attach(tree, parent, panel, ya::ui::canvasSlot().fill());

    auto form = std::make_shared<ya::UIContainer>("RenderForm");
    form->setPadding({16.0f, 12.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    ya::ui::attach(tree, *panel, form, ya::ui::contentSlot().fill());

    tree.attach(*form, makeLabel("Render — correctness baseline (text, button, image, edge markers)"));
    tree.attach(*form, makeBodyText("Use this page as the first-frame render sanity target before deeper layout/event refactors."));

    auto markerRow = std::make_shared<ya::UIContainer>("RenderMarkers");
    markerRow->setDirection(ya::EWidgetBoxLayout::Horizontal);
    markerRow->setSpacing(8.0f);
    ya::ui::attach(tree, *form, markerRow, ya::ui::boxSlot().preferredSize({0.0f, 84.0f}));
    const auto addMarker = [&](const std::string& name, const std::string& label, const glm::vec4& color)
    {
        auto cell = std::make_shared<ya::UIBorder>(name);
        cell->setColor(color);
        ya::ui::attach(tree, *markerRow, cell, ya::ui::boxSlot().preferredSize({180.0f, 84.0f}));

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
    ya::ui::attach(tree, *imageRow, image, ya::ui::boxSlot().preferredSize({128.0f, 96.0f}));

    state.renderProbeButton           = makeDemoButton("RenderProbe", "Render Probe", 160.0f);
    state.renderProbeButton->_onClick = [&state, log]
    {
        ++state.renderProbeClicks;
        state.renderLog = std::format("Render probe clicked ({})", state.renderProbeClicks);
        log(state.renderLog);
    };
    ya::ui::attach(tree, *form, state.renderProbeButton, ya::ui::boxSlot().preferredSize({160.0f, 26.0f}));

    tree.attach(*form, makeBodyText("Expected: readable left-to-right text, stable clipping, no inversion, no flicker on resize."));
}

} // namespace guiworkbench
