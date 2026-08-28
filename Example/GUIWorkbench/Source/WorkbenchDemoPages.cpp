#include "WorkbenchDemoPages.h"

#include "Core/Log.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Dialog.h"
#include "GUI/Widgets/Controls/DockFloatingHost.h"
#include "GUI/Widgets/Controls/DockSpace.h"
#include "GUI/Widgets/Controls/DockWorkspace.h"
#include "GUI/Widgets/Controls/DragDrop.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/InputExtras.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/TableGrid.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"
#include "GUI/Widgets/Controls/TreeView.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Style.h"
#include "GUI/Widgets/WidgetTree.h"
#include "Render/Resources/FontManager.h"

#include <algorithm>
#include <format>

namespace guiworkbench
{

namespace
{

constexpr glm::vec4 kPanelColor  = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kHeaderColor = {0.55f, 0.60f, 0.68f, 1.0f};
// Button fills come from the mounted WorkbenchTheme ("button" key); these
// scaffold values were the pre-theme source and are retired (Phase 4).
constexpr glm::vec4 kTextColor = {0.88f, 0.90f, 0.94f, 1.0f};

std::shared_ptr<ya::UIText> makeLabel(const std::string& text, float fontSize = 13.0f)
{
    auto label        = std::make_shared<ya::UIText>(text + "_Label");
    label->_bAutoSize = true;
    label->_fontSize  = static_cast<uint32_t>(fontSize);
    label->setText(text);
    label->setColor(kHeaderColor);
    return label;
}

std::shared_ptr<ya::UIText> makeBodyText(const std::string& text)
{
    auto label        = std::make_shared<ya::UIText>(text + "_Body");
    label->_bAutoSize = true;
    label->_fontSize  = 13;
    label->setText(text);
    label->setColor(kTextColor);
    return label;
}

std::shared_ptr<ya::UIButton> makeDemoButton(const std::string& name, const std::string& label, float width = 0.0f)
{
    auto button = std::make_shared<ya::UIButton>(name);
    if (width > 0.0f) {
        button->setSize({width, 26.0f});
    }
    else {
        button->_bAutoSize = true;
        button->setContentPadding({12.0f, 4.0f});
    }

    auto text        = std::make_shared<ya::UIText>(name + "_Label");
    text->_bAutoSize = true;
    text->_fontSize  = 13;
    text->setText(label);
    // No authored color: the label resolves the theme "text" style, so a
    // light theme flips button labels to dark text (style-system Phase 4).
    text->_hAlign = ya::EWidgetAlignH::Center;
    text->_vAlign = ya::EWidgetAlignV::Center;
    button->addDetachedChild(text);
    return button;
}

std::shared_ptr<ya::UIContainer> makeRow(ya::WidgetTree& tree, ya::UIElement& parent, float spacing = 8.0f)
{
    auto row = std::make_shared<ya::UIContainer>("Row");
    row->setDirection(ya::EWidgetBoxLayout::Horizontal);
    row->setSpacing(spacing);
    tree.attach(parent, row);
    return row;
}

std::shared_ptr<ya::UIDragDropTile> makeDemoDragSource(std::string name,
                                                       std::string label,
                                                       std::string payload)
{
    auto tile = std::make_shared<ya::UIDragDropTile>(std::move(name), ya::UIDragDropTile::EKind::Source);
    tile->_label = std::move(label);
    auto behavior = std::make_shared<ya::UIDragSourceBehavior>();
    behavior->bCapturePointerOnPress = true;
    behavior->setPressedState = [](ya::UIElement& owner, bool bPressed)
    {
        if (auto* tile = dynamic_cast<ya::UIDragDropTile*>(&owner)) {
            tile->setPressed(bPressed);
        }
    };
    const std::string behaviorPayload = std::move(payload);
    behavior->operationFactory = [behaviorPayload, ghostLabel = tile->_label](ya::UIElement&) {
        auto operation = std::make_shared<ya::UIDragDropOperation>();
        operation->typeId = "workbench.payload";
        operation->payload = behaviorPayload;
        operation->ghostLabel = ghostLabel.empty() ? behaviorPayload : ghostLabel;
        return operation;
    };
    tile->addBehavior(behavior);
    return tile;
}

std::shared_ptr<ya::UIDragDropTile> makeDemoDropTarget(
    std::string name,
    std::string label,
    std::function<bool(const std::string& payload)> accept,
    std::function<void(const std::string& payload)> onDropped)
{
    auto tile = std::make_shared<ya::UIDragDropTile>(std::move(name), ya::UIDragDropTile::EKind::Target);
    tile->_label = std::move(label);
    auto behavior = std::make_shared<ya::UIDropTargetBehavior>();
    behavior->acceptPayload = [accept = std::move(accept)](ya::UIElement& owner, const std::string& payload, const glm::vec2& logicalPoint)
    {
        return owner.hitTestLayoutRect(logicalPoint) && (accept ? accept(payload) : !payload.empty());
    };
    behavior->handleDroppedPayload = [onDropped = std::move(onDropped)](ya::UIElement&, const std::string& payload, const glm::vec2&)
    {
        if (onDropped) {
            onDropped(payload);
        }
    };
    behavior->setHighlightState = [](ya::UIElement& owner, bool bHighlight)
    {
        if (auto* tile = dynamic_cast<ya::UIDragDropTile*>(&owner)) {
            tile->setHighlighted(bHighlight);
        }
    };
    tile->addBehavior(behavior);
    return tile;
}

/// Vector primitives showcase: lines (any angle, any thickness), a rectangle
/// outline and a cubic bezier, all drawn through the UIFrameBuilder vector
/// API (addLine/addRectOutline/addBezierCubic) — the same primitives the
/// editor's RenderGraph topology and drag-insert highlights will use.
struct FVectorDemoCanvas : public ya::UIElement
{
    explicit FVectorDemoCanvas(std::string name) : ya::UIElement(std::move(name)) {}

    void paintSelf(ya::UIFrameBuilder& builder) override
    {
        builder.addSprite(_layoutRect, {0.10f, 0.11f, 0.14f, 1.0f}, nullptr);

        const glm::vec2 o = _layoutRect.pos + glm::vec2(12.0f, 12.0f);

        // Horizontal / vertical / diagonal lines, 1px and 2px.
        builder.addLine(o, o + glm::vec2(120.0f, 0.0f), {0.35f, 0.80f, 0.55f, 1.0f}, 1.0f);
        builder.addLine(o + glm::vec2(0.0f, 24.0f), o + glm::vec2(120.0f, 24.0f), {0.35f, 0.80f, 0.55f, 1.0f}, 2.0f);
        builder.addLine(o + glm::vec2(0.0f, 48.0f), o + glm::vec2(120.0f, 72.0f), {0.85f, 0.65f, 0.30f, 1.0f}, 2.0f);

        // Rectangle outline.
        const ya::Rect2D rect{.pos = o + glm::vec2(150.0f, 0.0f), .extent = {90.0f, 56.0f}};
        builder.addRectOutline(rect, {0.45f, 0.60f, 0.90f, 1.0f}, 2.0f);

        // Cubic bezier (client-side tessellated into a polyline).
        builder.addBezierCubic(o + glm::vec2(270.0f, 70.0f),
                               o + glm::vec2(310.0f, -20.0f),
                               o + glm::vec2(360.0f, 140.0f),
                               o + glm::vec2(400.0f, 30.0f),
                               {0.90f, 0.45f, 0.70f, 1.0f},
                               2.0f,
                               32);
    }
};

} // namespace

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel        = std::make_shared<ya::UIPanel>("RenderDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form        = std::make_shared<ya::UIContainer>("RenderForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Render — correctness baseline (text, button, image, edge markers)"));
    tree.attach(*form, makeBodyText("Use this page as the first-frame render sanity target before deeper layout/event refactors."));

    auto markerRow = std::make_shared<ya::UIContainer>("RenderMarkers");
    markerRow->setDirection(ya::EWidgetBoxLayout::Horizontal);
    markerRow->setSpacing(8.0f);
    markerRow->setSize({0.0f, 84.0f});
    tree.attach(*form, markerRow);

    const auto addMarker = [&](const std::string& name, const std::string& label, const glm::vec4& color)
    {
        auto cell = std::make_shared<ya::UIPanel>(name);
        cell->setSize({180.0f, 84.0f});
        cell->setColor(color);
        tree.attach(*markerRow, cell);

        auto text        = makeBodyText(label);
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->_hAlign    = ya::EWidgetAlignH::Center;
        text->_vAlign    = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    };

    addMarker("TopLeftMarker", "Top-left", {0.37f, 0.18f, 0.18f, 1.0f});
    addMarker("CenterMarker", "Center", {0.18f, 0.33f, 0.24f, 1.0f});
    addMarker("BottomRightMarker", "Bottom-right", {0.18f, 0.25f, 0.38f, 1.0f});

    auto imageRow = makeRow(tree, *form);
    tree.attach(*imageRow, makeBodyText("Image placeholder"));
    auto image = std::make_shared<ya::UIImage>("RenderProbeImage");
    image->setSize({128.0f, 96.0f});
    image->_assetPath = "builtin/checkerboard";
    tree.attach(*imageRow, image);

    state.renderProbeButton           = makeDemoButton("RenderProbe", "Render Probe", 160.0f);
    state.renderProbeButton->_onClick = [&state, log]
    {
        ++state.renderProbeClicks;
        state.renderLog = std::format("Render probe clicked ({})", state.renderProbeClicks);
        log(state.renderLog);
    };
    tree.attach(*form, state.renderProbeButton);

    tree.attach(*form, makeBodyText("Expected: readable left-to-right text, stable clipping, no inversion, no flicker on resize."));
}

void buildWidgetsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto counter = ya::ui::button("Counter")
                       .child(ya::ui::text("Counter_Label")
                                  .setText(std::format("Clicked {} times", state.clickCount))
                                  .setFontSize(13)
                                  .setHAlign(ya::EWidgetAlignH::Center)
                                  .setVAlign(ya::EWidgetAlignV::Center))
                       .setSize({180.0f, 26.0f})
                       .setOnClick([&state, log]
                                   {
                           ++state.clickCount;
                           state.widgetLog = std::format("Button clicked (total {})", state.clickCount);
                           log(state.widgetLog); });
    state.counterButton = counter.share();

    auto checkA = ya::ui::checkBox("CheckA")
                      .setChecked(state.bCheckA)
                      .child(body("CheckA_Body", "Show grid lines"))
                      .setOnChanged([&state, log](bool bChecked)
                                    {
                          state.bCheckA = bChecked;
                          log(std::format("CheckBox '{}' -> {}", "CheckA", bChecked ? "on" : "off")); });
    state.checkA = checkA.share();

    auto slider = ya::ui::slider("BrightnessSlider")
                      .setSize({260.0f, 22.0f})
                      .setValue(state.sliderValue)
                      .setOnValueChanged([&state, log](float value)
                                         {
                          state.sliderValue = value;
                          log(std::format("Slider -> {:.2f}", value)); });
    state.slider = slider.share();

    auto combo = ya::ui::comboBox("ApiCombo")
                     .setSize({180.0f, 26.0f})
                     .setItems({"Vulkan", "OpenGL", "Metal", "DirectX 12"});
    state.combo = combo
                      .setSelectedIndex(std::clamp(
                          state.comboIndex, 0, static_cast<int>(combo.share()->_items.size()) - 1))
                      .setOnSelectionChanged([&state, log](int index)
                                             {
                          state.comboIndex = index;
                          if (state.combo) {
                              log(std::format("ComboBox -> {}", state.combo->currentLabel()));
                          } })
                      .share();

    auto page =
        ya::ui::panel("WidgetsDemo")
            .fillParent()
            .setColor(kPanelColor)
            .child(
                ya::ui::column("WidgetsForm")
                    .fillParent()
                    .setSize({0.0f, 0.0f})
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .children(
                        header("WidgetsTitle",
                               "Widgets — buttons, checkbox, slider, combo box, image, text input"),
                        std::move(counter),
                        std::move(checkA),
                        ya::ui::checkBox("CheckB")
                            .setChecked(state.bCheckB)
                            .child(body("CheckB_Body", "Enable shadows"))
                            .setOnChanged([&state, log](bool bChecked)
                                          {
                                        state.bCheckB = bChecked;
                                        log(std::format("CheckBox '{}' -> {}", "CheckB", bChecked ? "on" : "off")); }),
                        ya::ui::checkBox("CheckC")
                            .setChecked(state.bCheckC)
                            .child(body("CheckC_Body", "VSync"))
                            .setOnChanged([&state, log](bool bChecked)
                                          {
                                        state.bCheckC = bChecked;
                                        log(std::format("CheckBox '{}' -> {}", "CheckC", bChecked ? "on" : "off")); }),
                        ya::ui::row("Row")
                            .setSpacing(8.0f)
                            .children(
                                body("BrightnessLabel", "Brightness"),
                                std::move(slider)),
                        ya::ui::row("Row")
                            .setSpacing(8.0f)
                            .children(
                                body("ApiLabel", "Render API"),
                                std::move(combo)),
                        ya::ui::row("Row")
                            .setSpacing(8.0f)
                            .children(
                                body("TextureLabel", "Texture"),
                                ya::ui::image("DemoImage")
                                    .setSize({96.0f, 64.0f})
                                    .setAssetPath("builtin/checkerboard")),
                        ya::ui::row("Row")
                            .setSpacing(8.0f)
                            .children(
                                body("NotesLabel", "Notes"),
                                ya::ui::textField("NotesField")
                                    .setSize({220.0f, 26.0f})
                                    .setFontSize(13)
                                    .setText(state.textFieldValue)
                                    .setOnCommit([&state, log](const std::string& text)
                                                 {
                                                state.textFieldValue = text;
                                                log(std::format("TextField committed: '{}'", text)); }))));
    ya::ui::build(tree, parent, std::move(page));
}

void buildLayoutDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };
    auto cellLabel = [&](std::string key, const std::string& text)
    {
        return body(std::move(key), text)
            .fillParent()
            .setSize({0.0f, 0.0f})
            .setHAlign(ya::EWidgetAlignH::Center)
            .setVAlign(ya::EWidgetAlignV::Center);
    };

    auto hbox = ya::ui::row("DemoHBox")
                    .setSize({0.0f, 64.0f})
                    .setSpacing(state.layoutSpacing)
                    .setPadding({4.0f, 4.0f})
                    .setClipChildren(true);
    for (int i = 0; i < 3; ++i) {
        hbox.child(
            ya::ui::panel(std::format("HCell{}", i))
                .setSize({380.0f, 50.0f})
                .setColor({0.22f + i * 0.06f, 0.30f + i * 0.04f, 0.38f, 1.0f})
                .child(cellLabel(std::format("HCell{}_Body", i), std::format("Cell {}", i + 1))));
    }

    auto vbox = ya::ui::column("DemoVBox")
                    .setSize({0.0f, 140.0f})
                    .setSpacing(6.0f)
                    .setPadding({6.0f, 6.0f})
                    .setMainAxisAlignment(ya::EWidgetMainAxisAlignment::End)
                    .setClipChildren(true);
    for (int i = 0; i < 4; ++i) {
        vbox.child(
            ya::ui::panel(std::format("VCell{}", i))
                .setSize({100.0f, 50.0f})
                .setColor({0.30f + i * 0.05f, 0.22f, 0.42f, 1.0f})
                .child(cellLabel(std::format("VCell{}_Body", i), std::format("Row {}", i + 1))));
    }

    auto hboxRef = hbox.share();
    auto page =
        ya::ui::panel("LayoutDemo")
            .fillParent()
            .setColor(kPanelColor)
            .child(
                ya::ui::column("LayoutForm")
                    .fillParent()
                    .setSize({0.0f, 0.0f})
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .children(
                        header("LayoutTitle",
                               "Layout — VBox / HBox, spacing, padding, alignment, stretch anchors"),
                        body("LayoutHint",
                             "Resize the window: containers stretch via anchorMin/anchorMax = {0,0}..{1,1}."),
                        header("HBoxTitle", "HBox (horizontal container)"),
                        std::move(hbox),
                        header("VBoxTitle", "VBox with End alignment"),
                        std::move(vbox),
                        ya::ui::row("Row")
                            .setSpacing(8.0f)
                            .children(
                                body("SpacingLabel", "Spacing"),
                                ya::ui::slider("SpacingSlider")
                                    .setSize({220.0f, 22.0f})
                                    .setValue(state.layoutSpacing / 24.0f)
                                    .setOnValueChanged([&state, log, hboxRef](float value)
                                                       {
                                                state.layoutSpacing = value * 24.0f;
                                                if (hboxRef) {
                                                    hboxRef->setSpacing(state.layoutSpacing);
                                                }
                                                log(std::format("Spacing -> {:.1f}px", state.layoutSpacing)); }))));
    ya::ui::build(tree, parent, std::move(page));
}

void buildMenusDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto page =
        ya::ui::panel("MenusDemo")
            .fillParent()
            .setColor(kPanelColor)
            .child(
                ya::ui::column("MenusForm")
                    .fillParent()
                    .setSize({0.0f, 0.0f})
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .children(
                        header("MenusTitle",
                               "Menus & popups — the menu bar above opens popup menus"),
                        body("MenusHint",
                             "Click a menu-bar entry, hover to switch, Esc or outside click closes."),
                        ya::ui::button("PopupButton")
                            .child(ya::ui::text("PopupButton_Label")
                                       .setText("Open popup menu...")
                                       .setFontSize(13)
                                       .setHAlign(ya::EWidgetAlignH::Center)
                                       .setVAlign(ya::EWidgetAlignV::Center))
                            .setSize({180.0f, 26.0f})
                            .setOnClick([&tree, &state, log]
                                        {
                                        auto menu = ya::UIMenu::create({
                                            ya::UIMenu::FItem{.label = "New Document",
                                             .action = [&state, log]
                                             {
                                                 state.menuLog = "Menu: New Document";
                                                 log(state.menuLog);
                                             }},
                                            ya::UIMenu::FItem{.label = "Open File...",
                                             .action = [&state, log]
                                             {
                                                 state.menuLog = "Menu: Open File...";
                                                 log(state.menuLog);
                                             }},
                                            ya::UIMenu::FItem{.label = "Save",
                                             .action = [&state, log]
                                             {
                                                 state.menuLog = "Menu: Save";
                                                 log(state.menuLog);
                                             }},
                                            ya::UIMenu::FItem::separator(),
                                            ya::UIMenu::FItem{.label = "Quit",
                                             .action = [&state, log]
                                             {
                                                 state.menuLog = "Menu: Quit";
                                                 log(state.menuLog);
                                             }},
                                        });
                                        menu->openAt(tree, {300.0f, 220.0f}); }),
                        body("MenusKeys",
                             "Keyboard: Up/Down move, Enter activates, Esc closes.")));
    ya::ui::build(tree, parent, std::move(page));
}

void buildDragDropDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                       const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };

    auto sourceRow = ya::ui::row("DragSourceRow").setSpacing(8.0f);
    const std::vector<std::string> payloads = {"asset.texture.diffuse", "asset.mesh.cube", "asset.material.pbr"};
    for (const std::string& payload : payloads) {
        auto item = makeDemoDragSource("Drag_" + payload, payload, payload);
        item->setSize({160.0f, 30.0f});
        sourceRow.child(item);
        if (payload == payloads[0]) {
            state.dragItem = item;
        }
    }

    auto zone        = makeDemoDropTarget("DropZone", "Drop zone", {}, [&state, log](const std::string& payload)
    {
        state.dropLog = std::format("Dropped '{}'", payload);
        log(state.dropLog);
    });
    zone->_anchorMin = {0.0f, 0.0f};
    zone->_anchorMax = {1.0f, 0.0f};
    zone->setPosition({0.0f, 12.0f});
    zone->setSize({0.0f, 120.0f});
    state.dropZone = zone;

    auto page = ya::ui::panel("DragDropDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(
                        ya::ui::column("DragDropForm")
                            .fillParent()
                            .setSize({0.0f, 0.0f})
                            .setPadding({16.0f, 12.0f})
                            .setSpacing(10.0f)
                            .children(
                                header("DragDropTitle", "Drag & drop — press an item, drag onto the zone"),
                                std::move(sourceRow),
                                zone));
    ya::ui::build(tree, parent, std::move(page));
}

void buildModalDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto openModal = ya::ui::button("OpenModal")
                         .child(ya::ui::text("OpenModal_Label")
                                    .setText("Open dialog...")
                                    .setFontSize(13)
                                    .setHAlign(ya::EWidgetAlignH::Center)
                                    .setVAlign(ya::EWidgetAlignV::Center))
                         .setSize({180.0f, 26.0f})
                         .setOnClick([&tree, &state, log]
                         {
                             if (state.bModalOpen) {
                                 return;
                             }
                             state.bModalOpen = true;

                             auto overlay = std::make_shared<ya::UIPopupOverlay>("ModalOverlay");
                             // Popup role: the shield is transparent (the page stays fully visible
                             // behind the dialog) and only swallows presses outside the content.
                             overlay->_bModal     = false;
                             overlay->_contentPos = {440.0f, 300.0f};

                             auto dialog = std::make_shared<ya::UIPanel>("ModalDialog");
                             dialog->setSize({360.0f, 170.0f});
                             dialog->setStyleKey("panel");
                             overlay->addDetachedChild(dialog);

                             auto stack        = std::make_shared<ya::UIContainer>("ModalStack");
                             stack->_anchorMin = {0.0f, 0.0f};
                             stack->_anchorMax = {1.0f, 1.0f};
                             // Inset via box padding, not position: an anchor span stretches to the
                             // parent's full size, so position offsets on top overflow the dialog's
                             // right/bottom edges. Padding shrinks the content rect instead.
                             stack->setPadding({16.0f, 14.0f});
                             stack->setSize({0.0f, 0.0f});
                             stack->setDirection(ya::EWidgetBoxLayout::Vertical);
                             stack->setSpacing(12.0f);
                             stack->setClipChildren(true);
                             dialog->addDetachedChild(stack);

                             auto title = makeLabel("About / New Project", 14.0f);
                             stack->addDetachedChild(title);

                             auto nameField = std::make_shared<ya::UITextField>("ModalName");
                             nameField->setSize({320.0f, 26.0f});
                             nameField->_fontSize = 13;
                             nameField->setText(state.modalName);
                             stack->addDetachedChild(nameField);
                             // Keep the field at its fixed width instead of stretching it across
                             // the dialog (box cross-axis default is Stretch), so its text never
                             // renders outside the dialog border.
                             if (auto* slot = stack->getBoxSlot(*nameField)) {
                                 slot->setCrossAlignment(ya::EUIBoxSlotCrossAlignment::Start);
                             }

                             auto buttons = std::make_shared<ya::UIContainer>("ModalButtons");
                             buttons->setDirection(ya::EWidgetBoxLayout::Horizontal);
                             buttons->setSpacing(8.0f);
                             stack->addDetachedChild(buttons);

                             auto okButton      = makeDemoButton("ModalOK", "OK", 80.0f);
                             okButton->_onClick = [&state, overlay, nameField, log]
                             {
                                 state.modalName = nameField->_text;
                                 log(std::format("Modal OK: '{}'", state.modalName));
                                 overlay->close();
                             };
                             buttons->addDetachedChild(okButton);

                             auto cancelButton      = makeDemoButton("ModalCancel", "Cancel", 80.0f);
                             cancelButton->_onClick = [&state, overlay, log]
                             {
                                 log("Modal cancelled");
                                 overlay->close();
                             };
                             buttons->addDetachedChild(cancelButton);

                             overlay->_onDismiss = [&state]()
                             { state.bModalOpen = false; };
                             overlay->open(tree);
                         });
    state.openModalButton = openModal.share();

    auto page = ya::ui::panel("ModalDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(
                        ya::ui::column("ModalForm")
                            .fillParent()
                            .setSize({0.0f, 0.0f})
                            .setPadding({16.0f, 12.0f})
                            .setSpacing(10.0f)
                            .children(
                                header("ModalTitle",
                                       "Popup dialog — a transparent shield swallows outside clicks, no dimming"),
                                std::move(openModal),
                                body("ModalHint",
                                     "Esc or clicking outside the dialog closes it; the page behind stays visible.")));
    ya::ui::build(tree, parent, std::move(page));
}

void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto list = ya::ui::column("DemoScrollList")
                    .fillParent()
                    .setSpacing(2.0f)
                    .setPadding({6.0f, 6.0f});
    // Enough rows to overflow the left pane at the default 1280x800 host
    // (and the taller 1440x900 step). 24 rows fit inside the stretched split
    // and leave maxOffset at 0, so wheel assertions never fire.
    for (int i = 0; i < 40; ++i) {
        list.child(
            ya::ui::panel(std::format("ScrollRow{}", i))
                .setSize({0.0f, 24.0f})
                .setColor({0.18f + (i % 3) * 0.04f, 0.20f, 0.24f, 1.0f})
                .child(body(std::format("ScrollRow{}_Body", i), std::format("Scrollable entry {}", i + 1))
                           .fillParent()
                           .setSize({0.0f, 0.0f})
                           .setVAlign(ya::EWidgetAlignV::Center)
                           .setPosition({8.0f, 0.0f})));
    }

    auto split = ya::ui::splitPane("DemoSplit")
                     .setSize({0.0f, 0.0f})
                     .setSplitRatio(0.38f)
                     .setMinFirstExtent(120.0f)
                     .setMinSecondExtent(160.0f)
                     .children(
                         ya::ui::scroll("DemoScroll").child(std::move(list)),
                         ya::ui::panel("DemoSplitRight")
                             .setColor({0.24f, 0.30f, 0.40f, 1.0f})
                             .child(body("DemoSplitRight_Body", "Drag the divider between panes\nWheel scrolls the list")
                                        .fillParent()
                                        .setSize({0.0f, 0.0f})
                                        .setHAlign(ya::EWidgetAlignH::Center)
                                        .setVAlign(ya::EWidgetAlignV::Center)));

    auto layout = ya::ui::column("ScrollSplitLayout")
                      .fillParent()
                      .setSize({0.0f, 0.0f})
                      .setPadding({16.0f, 12.0f})
                      .setSpacing(10.0f)
                      .children(
                          header("ScrollSplitTitle", "Scroll viewport + split pane — drag the divider"),
                          body("ScrollSplitHint",
                               "The split stretches with the window; hover the divider to grab it."));
    layout.childFill(std::move(split));

    auto page = ya::ui::panel("ScrollSplitDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(std::move(layout));
    ya::ui::build(tree, parent, std::move(page));
    (void)state;
    (void)log;
}

void buildGalleryDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log,
                      const std::function<void(bool bDark)>&         onToggleTheme)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };
    auto paneLabel = [&](std::string key, const std::string& text)
    {
        return body(std::move(key), text)
            .fillParent()
            .setHAlign(ya::EWidgetAlignH::Center)
            .setVAlign(ya::EWidgetAlignV::Center);
    };
    auto demoButton = [](std::string name, const std::string& label, float width)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(13)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        if (width > 0.0f) {
            return std::move(button).setSize({width, 26.0f});
        }
        return std::move(button).setContentPadding({12.0f, 4.0f});
    };

    // The gallery is taller than the viewport: scroll so every section stays
    // reachable and nothing paints over the status bar.
    auto form = ya::ui::column("GalleryForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);

    // ---------------------------------------------------------------------
    // Section 1 — Reactive data binding (model -> view, no manual repaint).
    // Every widget below is driven by a Reactive<T>; mutating the ref via
    // set() marks only the dependent widgets dirty through the reactive
    // invalidation layer.
    // ---------------------------------------------------------------------
    form.child(header("GallerySection1", "1. Reactive binding — model drives view"));

    auto counterRef    = std::make_shared<ya::Reactive<int>>(0);
    auto counterStrRef = std::make_shared<ya::Reactive<std::string>>("Count: 0");
    form.child(ya::ui::text("GalleryBoundCounter").bindText(counterStrRef).setFontSize(14));
    form.child(demoButton("GalleryInc", "Increment (reactive)", 200.0f).setOnClick(
        [counterRef, counterStrRef, log]
        {
            const int next = counterRef->value() + 1;
            counterRef->set(next);
            counterStrRef->set(std::format("Count: {}", next));
            log(std::format("Reactive counter -> {}", next));
        }));

    auto enabledRef = std::make_shared<ya::Reactive<bool>>(true);
    form.child(demoButton("GalleryDependent", "Enabled by reactive flag", 220.0f).bindEnabled(enabledRef));
    form.child(demoButton("GalleryToggle", "Toggle enabled flag", 220.0f).setOnClick(
        [enabledRef, log]
        {
            const bool next = !enabledRef->value();
            enabledRef->set(next);
            log(std::format("Reactive enabled flag -> {}", next ? "on" : "off"));
        }));

    auto menuLabelRef = std::make_shared<ya::Reactive<std::string>>("Dynamic Item");
    auto localBar     = std::make_shared<ya::UIMenuBar>("GalleryMenuBar");
    localBar->setSize({0.0f, 28.0f});
    auto* dynItem = localBar->addItem("Dynamic Item", nullptr);
    dynItem->bindLabel(menuLabelRef);
    form.child(localBar);
    form.child(demoButton("GalleryRename", "Rename menu item (reactive)", 260.0f).setOnClick(
        [menuLabelRef, log]
        {
            const std::string next = menuLabelRef->value() == "Dynamic Item" ? "Renamed!" : "Dynamic Item";
            menuLabelRef->set(next);
            log(std::format("Reactive menu label -> '{}'", next));
        }));

    auto ratioRef = std::make_shared<ya::Reactive<float>>(0.45f);
    form.child(
        ya::ui::splitPane("GallerySplit")
            .setSize({0.0f, 120.0f})
            .bindSplitRatio(ratioRef)
            .setMinFirstExtent(80.0f)
            .setMinSecondExtent(80.0f)
            .children(
                ya::ui::panel("GallerySplitLeft")
                    .setColor({0.20f, 0.24f, 0.32f, 1.0f})
                    .child(paneLabel("GallerySplitLeft_Body", "ratio <- reactive")),
                ya::ui::panel("GallerySplitRight")
                    .setColor({0.28f, 0.22f, 0.32f, 1.0f})
                    .child(paneLabel("GallerySplitRight_Body", "drag divider"))),
        ya::FBoxSlotArgs{.sizeRule = ya::EUIBoxSlotSizeRule::Fill});
    form.child(demoButton("GalleryRatio", "Set ratio 0.25 (reactive)", 240.0f).setOnClick(
        [ratioRef, log]
        {
            ratioRef->set(0.25f);
            log("Reactive split ratio -> 0.25");
        }));

    // ---------------------------------------------------------------------
    // Section 2 — TreeView (data-driven widget) + selection as a reactive
    // source. The selected node id is a Reactive<string> that a bound label
    // subscribes to: selecting a row updates the label with no manual wiring.
    // ---------------------------------------------------------------------
    form.child(header("GallerySection2", "2. TreeView (data-driven widget) + reactive selection"));

    auto roots = std::make_shared<ya::ReactiveList<ya::UITreeView::FNode>>();
    roots->push({
        .id       = "root",
        .label    = "Scene Root",
        .children = {
            {"mesh", "Mesh", {}},
            {"light", "Light", {
                                   {"point", "Point Light", {}},
                                   {"spot", "Spot Light", {}},
                               }},
            {"camera", "Camera", {}},
        },
    });
    roots->push({
        .id       = "ui",
        .label    = "UI",
        .children = {
            {"hud", "HUD", {}},
            {"menu", "Menu", {}},
        },
    });

    auto treeView = std::make_shared<ya::UITreeView>("GalleryTree");
    // AutoSize: the tree grows/shrinks with the expanded-row count, so
    // expanding never overflows the widget rect (the framework also clips
    // the tree's own paint to its rect as a backstop).
    treeView->_bAutoSize = true;
    treeView->setSize({0.0f, 0.0f});
    treeView->bindData(roots);
    treeView->setExpanded("root", true);
    treeView->setReorderable(true);
    treeView->setOnReorderHandler([roots, log](const std::string& fromId, const std::string& toId, int mode)
    {
        // Minimal demo reorder: move `fromId` within the ROOT list only
        // (before / after a root, or into a root as its child). The host
        // owns the data mutation — the tree only reports the intent.
        if (fromId == toId) {
            return;
        }
        std::vector<ya::UITreeView::FNode> snapshot;
        for (size_t i = 0; i < roots->size(); ++i) {
            snapshot.push_back(roots->get(i));
        }
        ya::UITreeView::FNode              moved;
        bool                               bFound = false;
        std::vector<ya::UITreeView::FNode> rebuilt;
        for (auto& n : snapshot) {
            if (n.id == fromId) {
                moved  = n;
                bFound = true;
            }
            else {
                rebuilt.push_back(n);
            }
        }
        if (!bFound) {
            return;
        }
        int targetIndex = -1;
        for (size_t i = 0; i < rebuilt.size(); ++i) {
            if (rebuilt[i].id == toId) {
                targetIndex = static_cast<int>(i);
                break;
            }
        }
        if (targetIndex < 0) {
            return;
        }
        if (mode == 1) {
            const std::function<bool(const ya::UITreeView::FNode&, const std::string&)> contains =
                [&](const ya::UITreeView::FNode& n, const std::string& id) -> bool
            {
                if (n.id == id) {
                    return true;
                }
                for (const auto& c : n.children) {
                    if (contains(c, id)) {
                        return true;
                    }
                }
                return false;
            };
            if (contains(moved, toId)) {
                log(std::format("Tree reorder refused: '{}' cannot move into its own descendant '{}'", fromId, toId));
                return;
            }
            rebuilt[static_cast<size_t>(targetIndex)].children.push_back(moved);
        }
        else {
            const int insertIndex = targetIndex + (mode == 2 ? 1 : 0);
            rebuilt.insert(rebuilt.begin() + insertIndex, moved);
        }
        roots->clear();
        for (const auto& n : rebuilt) {
            roots->push(n);
        }
        log(std::format("Tree reorder '{}' {} '{}'", fromId, mode == 0 ? "before" : (mode == 1 ? "into" : "after"), toId));
    });
    treeView->_onToggleExpanded = [log](const std::string& id, bool bExpanded)
    {
        log(std::format("Tree toggle '{}' -> {}", id, bExpanded ? "expanded" : "collapsed"));
    };
    treeView->_onContextMenu = [log](const std::string& nodeId, const glm::vec2&)
    {
        log(std::format("Tree context menu -> '{}'", nodeId));
    };
    auto treeFilterRef = std::make_shared<ya::Reactive<std::string>>("");
    treeView->bindFilter(treeFilterRef);
    auto selStrRef = std::make_shared<ya::Reactive<std::string>>("(none)");
    treeView->_onSelectionChanged = [selStrRef, log](const std::string& id)
    {
        selStrRef->set(id.empty() ? "(none)" : id);
        log(std::format("Tree selection -> '{}'", id));
    };
    form.child(treeView);
    form.child(ya::ui::row("GalleryFilterRow")
                   .setSpacing(8.0f)
                   .children(
                       body("GalleryFilter_Body", "Filter"),
                       ya::ui::textField("GalleryTreeFilter")
                           .setSize({160.0f, 24.0f})
                           .setFontSize(13)
                           .setOnTextChanged([treeFilterRef](const std::string& text)
                                             { treeFilterRef->set(text); })));
    form.child(ya::ui::text("GallerySelected").bindText(selStrRef).setFontSize(13));

    // ---------------------------------------------------------------------
    // Section 3 — Style system. The tree-level UITheme (WorkbenchTheme,
    // mounted on the WidgetTree by the app) resolves the "text" key: editing
    // one named style in the theme restyles every un-authored text that opt
    // in (badges below). The legacy FWidgetStyle/bindStyle path is retired
    // from app content (style-system Phase 4, unified binding path).
    // ---------------------------------------------------------------------
    form.child(header("GallerySection3", "3. Style system — tree theme restyles the group"));
    form.child(ya::ui::text("GalleryStyledText").setText("Styled text (themed)").setFillBackground(true));
    form.child(ya::ui::text("GalleryStyledCaption")
                   .setText("Another themed text resolving the same \"text\" key")
                   .setFillBackground(true));
    auto bDarkRef = std::make_shared<bool>(true);
    form.child(demoButton("GalleryTheme", "Toggle theme (dark/white)", 260.0f).setOnClick(
        [bDarkRef, onToggleTheme, log]
        {
            *bDarkRef = !*bDarkRef;
            onToggleTheme(*bDarkRef);
            log(std::format("Tree theme -> {}", *bDarkRef ? "dark" : "white"));
        }));

    // ---------------------------------------------------------------------
    // Section 4 — Vector primitives (UIFrameBuilder addLine / addRectOutline
    // / addBezierCubic). The building blocks for RenderGraph topology wires
    // and drag-insert highlight lines in the editor.
    // ---------------------------------------------------------------------
    form.child(header("GallerySection4", "4. Vector primitives — lines, outline, bezier"));
    auto vectorCanvas = std::make_shared<FVectorDemoCanvas>("GalleryVectorCanvas");
    vectorCanvas->setSize({430.0f, 110.0f});
    form.child(vectorCanvas,
               ya::FBoxSlotArgs{.crossAlignment = ya::EUIBoxSlotCrossAlignment::Start});

    // ---------------------------------------------------------------------
    // Section 5 — Table/Grid (data-driven UITableGrid: reactive row source +
    // reactive selection, grid separators drawn via the vector primitives).
    // ---------------------------------------------------------------------
    form.child(header("GallerySection5", "5. Table — data-driven grid with reactive selection"));

    auto tableRows = std::make_shared<ya::ReactiveList<ya::UITableGrid::FTableRow>>();
    tableRows->push({"hdr", {"Name", "Type", "Count", "Visible"}});
    tableRows->push({"r1", {"Cube", "StaticMesh", "3", "yes"}});
    tableRows->push({"r2", {"PointLight", "Light", "2", "yes"}});
    tableRows->push({"r3", {"Camera", "Node", "1", "no"}});
    tableRows->push({"r4", {"Material", "Asset", "8", "yes"}});

    auto tableGrid = std::make_shared<ya::UITableGrid>("GalleryTableGrid");
    tableGrid->setSize({400.0f, 0.0f});
    tableGrid->_bAutoSize    = true;
    tableGrid->_columnWidths = {140.0f, 100.0f, 0.0f, 0.0f}; // last two stretch
    tableGrid->bindData(tableRows);
    auto tableSelRef = std::make_shared<ya::Reactive<int>>(1);
    tableGrid->bindSelection(tableSelRef);
    tableGrid->_onSelectionChanged = [log](int row)
    {
        log(std::format("Table row -> {}", row));
    };
    auto cellButton = demoButton("GalleryCellButton", "Inspect", 0.0f);
    auto cellLive   = cellButton.share();
    tableGrid->addDetachedChild(cellButton.release());
    if (auto* cellSlot = tableGrid->getCellSlot(*cellLive)) {
        cellSlot->setCell(3, 2);
    }
    form.child(tableGrid, ya::FBoxSlotArgs{.crossAlignment = ya::EUIBoxSlotCrossAlignment::Start});
    form.child(body("GalleryTableCaption_Body",
                    "Click a row to select (reactive selection ref); row 3 / col 2 holds a real button widget."));

    // ---------------------------------------------------------------------
    // Section 6 — Input controls (DragFloat / SpinBox / RadioButton /
    // ColorEdit / SearchComboBox — the editor's ManipulateSpec-driven
    // property editors need these).
    // ---------------------------------------------------------------------
    form.child(header("GallerySection6", "6. Input controls — drag, spin, radio, color, search combo"));

    auto dragFloat = std::make_shared<ya::UIDragFloat>("GalleryDragFloat");
    dragFloat->setSize({120.0f, 24.0f});
    dragFloat->_value          = 3.5f;
    dragFloat->_onValueChanged = [log](float v)
    { log(std::format("DragFloat -> {:.2f}", v)); };
    form.child(ya::ui::row("GalleryDragRow").setSpacing(8.0f).children(body("GalleryDragFloat_Body", "DragFloat"), dragFloat));

    auto spinBox = std::make_shared<ya::UISpinBox>("GallerySpinBox");
    spinBox->setSize({120.0f, 24.0f});
    spinBox->_value          = 8.0f;
    spinBox->_step           = 1.0f;
    spinBox->_onValueChanged = [log](float v)
    { log(std::format("SpinBox -> {:.2f}", v)); };
    form.child(ya::ui::row("GallerySpinRow").setSpacing(8.0f).children(body("GallerySpinBox_Body", "SpinBox"), spinBox));

    auto radioRow = ya::ui::row("GalleryRadioRow").setSpacing(8.0f).child(body("GalleryRadio_Body", "Radio"));
    auto radios   = std::make_shared<std::vector<std::shared_ptr<ya::UIRadioButton>>>();
    const std::vector<std::string> radioLabels = {"Shadow", "CSM", "None"};
    for (size_t i = 0; i < radioLabels.size(); ++i) {
        auto radio = std::make_shared<ya::UIRadioButton>(std::format("GalleryRadio{}", i));
        radio->setSize({90.0f, 22.0f});
        radio->_label    = radioLabels[i];
        radio->_bChecked = i == 0;
        radio->_onSelect = [radios, log, label = radioLabels[i]](ya::UIRadioButton* self)
        {
            for (auto& r : *radios) {
                r->setChecked(r.get() == self);
            }
            log(std::format("Radio -> {}", label));
        };
        radios->push_back(radio);
        radioRow.child(radio);
    }
    form.child(std::move(radioRow));

    auto colorEdit = std::make_shared<ya::UIColorEdit>("GalleryColorEdit");
    colorEdit->setSize({170.0f, 28.0f});
    colorEdit->_color          = {0.24f, 0.46f, 0.82f, 1.0f};
    colorEdit->_onColorChanged = [log](const glm::vec4& c)
    {
        log(std::format("Color -> ({:.2f}, {:.2f}, {:.2f}, {:.2f})", c.r, c.g, c.b, c.a));
    };
    form.child(ya::ui::row("GalleryColorRow").setSpacing(8.0f).children(body("GalleryColorEdit_Body", "ColorEdit"), colorEdit));

    auto searchCombo = std::make_shared<ya::UISearchComboBox>("GallerySearchCombo");
    searchCombo->setSize({180.0f, 24.0f});
    searchCombo->_items              = {"Cube", "Sphere", "Capsule", "Plane", "Cone", "Torus"};
    searchCombo->_selectedIndex      = 0;
    searchCombo->_onSelectionChanged = [log](int index)
    { log(std::format("SearchCombo -> {}", index)); };
    form.child(ya::ui::row("GallerySearchRow").setSpacing(8.0f).children(body("GallerySearchCombo_Body", "SearchCombo"), searchCombo));

    // ---------------------------------------------------------------------
    // Section 7 — Drag & drop using ordinary UIElement subclasses.
    // ---------------------------------------------------------------------
    form.child(header("GallerySection7", "7. Drag & drop — sources onto targets"));

    auto dragDemoRow = ya::ui::row("GalleryDragSrcRow").setSpacing(8.0f);
    for (int i = 0; i < 3; ++i) {
        auto source = makeDemoDragSource(std::format("GalleryDragSrc{}", i),
                                         std::format("Item {}", i + 1),
                                         std::format("payload.{}", i + 1));
        source->setSize({110.0f, 26.0f});
        dragDemoRow.child(source);
    }
    form.child(std::move(dragDemoRow));

    auto dropResult = std::make_shared<ya::Reactive<std::string>>("(drop something here)");
    auto dropZoneA  = makeDemoDropTarget("GalleryDropA",
                                         "Zone A: accepts any",
                                         {},
                                         [dropResult, log](const std::string& payload)
                                         {
                                             dropResult->set(std::format("Zone A <- {}", payload));
                                             log(std::format("Dropped '{}' on Zone A", payload));
                                         });
    dropZoneA->setSize({180.0f, 60.0f});
    auto dropZoneB = makeDemoDropTarget("GalleryDropB",
                                        "Zone B: only payload.2",
                                        [](const std::string& payload) { return payload == "payload.2"; },
                                        [dropResult, log](const std::string& payload)
                                        {
                                            dropResult->set(std::format("Zone B <- {}", payload));
                                            log(std::format("Dropped '{}' on Zone B", payload));
                                        });
    dropZoneB->setSize({180.0f, 60.0f});
    form.child(ya::ui::row("GalleryDropRow")
                   .setSpacing(8.0f)
                   .children(
                       ya::ui::text("GalleryDropResult").bindText(dropResult).setFontSize(13),
                       dropZoneA,
                       dropZoneB));
    form.child(body("GalleryExpected_Body",
                    "Expected: incrementing, toggling, renaming, resizing, selecting and theming all update their targets without the page rebuilding."));

    auto page = ya::ui::panel("GalleryDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(ya::ui::scroll("GalleryScroll").fillParent().child(std::move(form)));
    ya::ui::build(tree, parent, std::move(page));
    (void)state;
}



void buildInteractionsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                           const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto demoButton = [](std::string name, const std::string& label, float width)
    {
        auto button = ya::ui::button(name).child(
            ya::ui::text(name + "_Label")
                .setText(label)
                .setFontSize(13)
                .setHAlign(ya::EWidgetAlignH::Center)
                .setVAlign(ya::EWidgetAlignV::Center));
        if (width > 0.0f) {
            return std::move(button).setSize({width, 26.0f});
        }
        return std::move(button).setContentPadding({12.0f, 4.0f});
    };

    auto form = ya::ui::column("InteractionsForm")
                    .fillParent()
                    .setSize({0.0f, 0.0f})
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(12.0f);

    form.child(header("InteractionsTooltipHeader", "Tooltip & wrapped text (editor-parity P6)"));
    form.child(ya::ui::row("InteractionsTipRow")
                   .child(demoButton("TooltipBtn", "Hover me (tooltip)", 200.0f)
                              .setTooltip("This tooltip appears after a 0.5s hover dwell.")));
    form.child(ya::ui::text("WrappedText")
                   .setText("This paragraph demonstrates automatic text wrapping. Long content breaks onto "
                            "multiple lines instead of overflowing its box, matching the editor's TextWrapped "
                            "behavior. CJK text also wraps: 中文换行测试中文换行测试。")
                   .setFontSize(13)
                   .setWrap(true)
                   .setMaxWrapWidth(360.0f),
               ya::FBoxSlotArgs{.crossAlignment = ya::EUIBoxSlotCrossAlignment::Start});

    // Subtree disable: the whole group goes input-inert when disabled.
    form.child(header("InteractionsDisableHeader", "Subtree disable"));
    auto group = ya::ui::row("DisableGroup")
                     .setSpacing(8.0f)
                     .children(
                         demoButton("GroupBtnA", "Group button A", 140.0f)
                             .setOnClick([log] { log("Group button A clicked"); }),
                         demoButton("GroupBtnB", "Group button B", 140.0f)
                             .setOnClick([log] { log("Group button B clicked"); }));
    auto groupHandle   = group.share();
    auto bGroupEnabled = std::make_shared<bool>(true);
    form.child(std::move(group));
    form.child(demoButton("ToggleGroupBtn", "Toggle group enabled", 200.0f)
                   .setOnClick(
                       [groupHandle, bGroupEnabled, log]
                       {
                           *bGroupEnabled = !*bGroupEnabled;
                           groupHandle->setEnabled(*bGroupEnabled);
                           const std::string groupState = *bGroupEnabled ? "enabled" : "disabled";
                           log(std::format("Group {}", groupState));
                       }));

    // Modal dialog is assembled on click (same as Menus / Modal: event-time live API).
    form.child(header("InteractionsDialogHeader", "Modal dialog"));
    form.child(demoButton("OpenDialogBtn", "Open dialog...", 180.0f)
                   .setOnClick(
                       [&tree, log]
                       {
                           auto content = ya::ui::text("DialogContent")
                                              .setText("This is a modal dialog built on UIPopupOverlay's modal role.")
                                              .setFontSize(13)
                                              .setColor({0.88f, 0.90f, 0.94f, 1.0f})
                                              .release();
                           auto dialog       = ya::UIDialog::create("Confirm", std::move(content));
                           dialog->_onClosed = [log](bool bConfirmed)
                           {
                               const std::string result = bConfirmed ? "confirmed" : "cancelled";
                               log(std::format("Dialog closed: {}", result));
                           };
                           dialog->open(tree);
                       }));

    auto page = ya::ui::panel("InteractionsDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(std::move(form));
    ya::ui::build(tree, parent, std::move(page));
    (void)state;
}



void buildDockDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log)
{
    // The floating host lives on the tree's Popup layer, outside the demo
    // content host, so a page rebuild never tears it down automatically.
    // Detach the previous instance explicitly, otherwise every re-entry into
    // the Dock page accumulates a host + workspace under the Popup layer.
    if (state.dockFloatingHost && state.dockFloatingHost->getTree() == &tree) {
        tree.detach(*state.dockFloatingHost);
    }
    state.dockFloatingHost.reset();

    auto dockWs            = std::make_shared<ya::UIDockWorkspace>();
    dockWs->bAllowFloating = true;
    dockWs->bAllowTearOff  = true;

    auto dock        = std::make_shared<ya::UIDockSpace>("DemoDock");
    dock->_anchorMin = {0.0f, 0.0f};
    dock->_anchorMax = {1.0f, 1.0f};
    dock->setWorkspace(dockWs);

    auto page = ya::ui::column("DockDemo").fillParent().setSize({0.0f, 0.0f});
    page.childFill(dock);
    ya::ui::build(tree, parent, std::move(page));

    auto floatHost = std::make_shared<ya::UIDockFloatingHost>("DemoFloatingHost");
    floatHost->bindWorkspace(dockWs);
    const ya::WidgetAttachment floatingAttached =
        tree.attachToLayer(ya::WidgetTree::ELayer::Popup, floatHost);
    YA_CORE_ASSERT(floatingAttached.valid(), "Dock floating host attach failed");
    state.dockFloatingHost = floatHost;

    const auto makePanel = [](const std::string& name, const std::string& text)
    {
        return ya::ui::panel(name + "_Body")
            .setStyleKey("panel.canvas")
            .child(ya::ui::text(name + "_Label")
                       .setText(text)
                       .setFontSize(14)
                       .fillParent()
                       .setPosition({12.0f, 12.0f})
                       .setSize({-24.0f, -24.0f})
                       .setHAlign(ya::EWidgetAlignH::Center)
                       .setVAlign(ya::EWidgetAlignV::Center))
            .release();
    };

    const ya::DockPanelId sceneId     = dockWs->addPanel("Scene", makePanel("Scene", "Scene viewport"));
    const ya::DockPanelId hierarchyId = dockWs->addPanel("Hierarchy", makePanel("Hierarchy", "Actor hierarchy"));
    const ya::DockPanelId inspectorId = dockWs->addPanel("Inspector", makePanel("Inspector", "Inspector panel"));
    const ya::DockPanelId consoleId   = dockWs->addPanel("Console", makePanel("Console", "Console output"));
    const ya::DockPanelId assetsId    = dockWs->addPanel("Assets", makePanel("Assets", "Asset browser"));

    auto&                model    = dockWs->dockModel();
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
    dockWs->fireDockUpdated();
    log("Dock demo: drag tabs to split / merge, drag out to float, drag floating title to re-dock");

    (void)state;
}

void buildThemeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log,
                    const std::function<void(bool bDark)>&         onToggleTheme)
{
    auto header = [](std::string key, const std::string& text, uint32_t fontSize = 13)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(fontSize).setColor(kHeaderColor);
    };
    auto themedButton = [](std::string key, const std::string& label, float width)
    {
        std::string labelKey = key + "_Label";
        return ya::ui::button(std::move(key))
            .setSize({width, 26.0f})
            .child(ya::ui::text(std::move(labelKey))
                       .setText(label)
                       .setFontSize(13)
                       .setHAlign(ya::EWidgetAlignH::Center)
                       .setVAlign(ya::EWidgetAlignV::Center));
    };

    // Value-captured so the callback outlives this builder without dangling.
    auto bDark = std::make_shared<bool>(true);

    auto page = ya::ui::column("ThemeForm")
                    .setPadding({12.0f, 12.0f})
                    .setSpacing(8.0f)
                    .children(
                        header("ThemeTitle",
                               "Theme — white/dark toggle drives the tree-level UITheme"),
                        header("ThemeHint",
                               "Buttons below use style key \"button\"; toggling swaps the tree theme",
                               12),
                        themedButton("ThemeToggle", "Toggle theme (dark/white)", 240.0f)
                            .setOnClick([bDark, onToggleTheme, log]
                                        {
                                *bDark = !*bDark;
                                onToggleTheme(*bDark);
                                log(std::format("Theme -> {}", *bDark ? "dark" : "white")); }),
                        themedButton("ThemeShowButton", "Themed button", 180.0f),
                        themedButton("ThemeShowButton2", "Another themed button", 180.0f),
                        header("ThemeButtonHint", "(both buttons share style key \"button\")", 11),
                        ya::ui::panel("ThemeShowPanel").setSize({180.0f, 40.0f}),
                        header("ThemeShowCaption", "Panel resolves style key \"panel\"", 11));
    ya::ui::build(tree, parent, std::move(page));

    (void)state;
}

void buildUnicodeDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text, uint32_t fontSize = 13)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(fontSize).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto page =
        ya::ui::column("UnicodeForm")
            .setPadding({12.0f, 12.0f})
            .setSpacing(8.0f)
            .children(
                header("UnicodeTitle",
                       "Unicode — CJK + emoji through the font stack (SDF fallback + color atlas)"),
                body("UnicodeZh", "简体中文：你好，世界！这是一个字体栈测试。"),
                body("UnicodeJa", "日本語：こんにちは、世界。"),
                body("UnicodeKo", "한국어：안녕하세요, 세계."),
                body("UnicodeEmoji", "Emoji: 😀 🎉 🚀 ❤️ 🍕 ✅"),
                body("UnicodeMixed", "Mixed: 中文 + Latin + 123 + emoji 🎈"),
                header("UnicodeLargeTitle", "Large CJK title", 20),
                body("UnicodeLargeBody", "大字号中文标题：字体渲染验收"));
    ya::ui::build(tree, parent, std::move(page));

    (void)state;
    (void)log;
}

void buildChineseTest(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text, uint32_t fontSize = 13)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(fontSize).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto form = ya::ui::column("ChineseTestForm").setPadding({12.0f, 12.0f}).setSpacing(8.0f);
    form.child(header("ChineseTitle", "中文测试 — 单一 CJK fallback 字重一致性验收"));
    form.child(body("ChineseHint",
                    "本页只渲染中文，用于核对相邻字亮度/粗细是否一致、边缘是否发虚。"
                    "若字体栈把中文分散到多个 fallback 字体，相邻字会忽明忽暗、边缘发虚。"));

    // Multiple fixed sizes: the eye compares weight/edge across px without
    // any live rebuild. 13px is the reported problem size.
    struct Row
    {
        uint32_t    px;
        const char* tag;
    };
    static constexpr Row kRows[] = {
        {.px = 9, .tag = "小字"},
        {.px = 11, .tag = "小字"},
        {.px = 13, .tag = "正文"},
        {.px = 16, .tag = "中字"},
        {.px = 20, .tag = "大字"},
        {.px = 24, .tag = "大字"},
        {.px = 32, .tag = "特大"},
        {.px = 40, .tag = "特大"},
    };
    for (const auto& r : kRows) {
        form.child(header(std::format("CjkRow{}", r.px),
                          std::format("{} {}px：字体渲染验收测试中文连续文本", r.tag, r.px),
                          r.px));
    }

    form.child(header("ChineseParaTitle", "纯中文段落（连续文本）"));
    form.child(body("ChinesePara",
                    "渲染引擎字体子系统负责把缺失字形从中文备用字体解析出来，保证同一段中文来自"
                    "同一个字体面孔，从而相邻字符的笔画粗细与亮度保持一致，避免出现一个字亮一个字"
                    "暗、边缘发虚的问题。这是中文渲染质量的验收段落，请观察每个字的黑白对比是否均匀。"));
    form.child(header("ChinesePunctTitle", "标点与字混合（逗号句号叹号括号问号）"));
    form.child(header("ChinesePunct", "中文，中文。中文！（中文）中文？中文；中文：中文、中文——"));
    form.child(header("ChineseGridTitle", "逐字黑白对比验收（一字一格，便于发现忽明忽暗）"));
    form.child(header("ChineseGrid",
                      "日 本 语 言 学 中 文 字 体 测 试 标 题 验 收 简 体 繁 体 汉 字 笔 画 粗 细 亮 度 边 缘"));

    ya::ui::build(tree, parent, std::move(form));
    state.statusText = "中文测试页面已构建（单一 CJK fallback：PingFang / msyh 优先）";
    (void)log;
}

void buildRoundedRectDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kHeaderColor);
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setColor(kTextColor);
    };

    auto grid = ya::ui::row("RoundedGrid").setSize({0.0f, 96.0f}).setSpacing(12.0f);
    struct Card
    {
        const char* name;
        const char* label;
        glm::vec4   color;
        float       radius;
    };
    static constexpr Card kCards[] = {
        {"Round0", "0px", {0.37f, 0.18f, 0.18f, 1.0f}, 0.0f},
        {"Round8", "8px", {0.18f, 0.33f, 0.24f, 1.0f}, 8.0f},
        {"Round16", "16px", {0.18f, 0.25f, 0.38f, 1.0f}, 16.0f},
        {"Round32", "32px", {0.36f, 0.30f, 0.14f, 1.0f}, 32.0f},
    };
    for (const auto& c : kCards) {
        grid.child(
            ya::ui::panel(c.name)
                .setSize({120.0f, 96.0f})
                .setColor(c.color)
                .setCornerRadius(c.radius)
                .child(body(std::format("{}_Body", c.name), std::format("r={}", c.label))
                           .fillParent()
                           .setHAlign(ya::EWidgetAlignH::Center)
                           .setVAlign(ya::EWidgetAlignV::Center)));
    }

    auto page = ya::ui::panel("RoundedRectDemo")
                    .fillParent()
                    .setColor(kPanelColor)
                    .child(
                        ya::ui::column("RoundedForm")
                            .fillParent()
                            .setSize({0.0f, 0.0f})
                            .setPadding({16.0f, 12.0f})
                            .setSpacing(12.0f)
                            .children(
                                header("RoundedTitle",
                                       "Rounded Rect — SDF corner radius (UIPanel.setCornerRadius)"),
                                body("RoundedHint",
                                     "Each card below is a solid-color UIPanel with a corner radius. The radius is a single "
                                     "tree-local logical-px value; the compose pass scales it to target px and the shader carves "
                                     "the corners via a signed-distance field (no texture, no atlas)."),
                                std::move(grid),
                                ya::ui::panel("RoundedNested")
                                    .setSize({280.0f, 110.0f})
                                    .setColor({0.16f, 0.20f, 0.28f, 1.0f})
                                    .setCornerRadius(20.0f)
                                    .children(
                                        header("RoundedNestedCaption",
                                               "Rounded container with a sharp inner panel")
                                            .setAnchors({0.10f, 0.20f}, {0.90f, 0.45f}),
                                        ya::ui::panel("RoundedNestedInner")
                                            .setColor({0.55f, 0.60f, 0.68f, 1.0f})
                                            .setAnchors({0.10f, 0.55f}, {0.90f, 0.85f})),
                                body("RoundedExpected",
                                     "Expected: top-left card is a sharp rectangle; the others show progressively rounder corners. "
                                     "The nested card keeps its rounded outer alpha while the inner panel stays sharp.")));
    ya::ui::build(tree, parent, std::move(page));
    state.statusText = "Rounded Rect demo built";
    (void)log;
}

} // namespace guiworkbench
