#include "WorkbenchDemoPages.h"

#include "Core/Log.h"

#include "Render/Resources/FontManager.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Dialog.h"
#include "GUI/Widgets/Controls/DockSpace.h"
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
#include "GUI/Widgets/Reactive.h"
#include "GUI/Widgets/Style.h"

#include <algorithm>
#include <format>

namespace guiworkbench
{

namespace
{

constexpr glm::vec4 kPanelColor   = {0.11f, 0.12f, 0.15f, 1.0f};
constexpr glm::vec4 kHeaderColor  = {0.55f, 0.60f, 0.68f, 1.0f};
constexpr glm::vec4 kButtonNormal  = {0.20f, 0.22f, 0.27f, 1.0f};
constexpr glm::vec4 kButtonHovered = {0.27f, 0.30f, 0.37f, 1.0f};
constexpr glm::vec4 kButtonPressed = {0.14f, 0.16f, 0.20f, 1.0f};
constexpr glm::vec4 kButtonFocused = {0.24f, 0.46f, 0.82f, 1.0f};
constexpr glm::vec4 kTextColor    = {0.88f, 0.90f, 0.94f, 1.0f};

std::shared_ptr<ya::UIText> makeLabel(const std::string& text, float fontSize = 13.0f)
{
    auto label = std::make_shared<ya::UIText>(text + "_Label");
    label->_bAutoSize = true;
    label->_fontSize  = static_cast<uint32_t>(fontSize);
    label->setText(text);
    label->_color     = kHeaderColor;
    return label;
}

std::shared_ptr<ya::UIText> makeBodyText(const std::string& text)
{
    auto label = std::make_shared<ya::UIText>(text + "_Body");
    label->_bAutoSize = true;
    label->_fontSize  = 13;
    label->setText(text);
    label->_color     = kTextColor;
    return label;
}

std::shared_ptr<ya::UIButton> makeDemoButton(const std::string& name, const std::string& label, float width = 0.0f)
{
    auto button = std::make_shared<ya::UIButton>(name);
    if (width > 0.0f) {
        button->setSize({width, 26.0f});
    }
    else {
        button->_bAutoSize      = true;
        button->setContentPadding({12.0f, 4.0f});
    }
    button->_normalColor  = kButtonNormal;
    button->_hoveredColor = kButtonHovered;
    button->_pressedColor = kButtonPressed;
    button->_focusedColor = kButtonFocused;

    auto text = std::make_shared<ya::UIText>(name + "_Label");
    text->_bAutoSize = true;
    text->_fontSize  = 13;
    text->setText(label);
    text->_color     = {0.92f, 0.94f, 0.97f, 1.0f};
    text->_hAlign    = ya::EWidgetAlignH::Center;
    text->_vAlign    = ya::EWidgetAlignV::Center;
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

/// Drag source list item: press then move past a threshold starts a tree
/// drag session with a string payload.
struct FDemoDragItem : public ya::UIElement
{
    FDemoDragItem(std::string name) : ya::UIElement(std::move(name))
    {
        _hitFilter = ya::EWidgetHitFilter::Stop;
    }

    std::string _payload;
    std::string _label;
    std::function<void()> _onDropped;

    void paintSelf(ya::UIFrameBuilder& builder) override
    {
        builder.addSprite(_layoutRect, {0.20f, 0.22f, 0.27f, 1.0f}, nullptr);
        auto font = ya::FontManager::get()->getFont(ya::DEFAULT_RUNTIME_FONT_NAME, 13);
        if (font) {
            builder.addText(_layoutRect, _label, {0.92f, 0.94f, 0.97f, 1.0f}, font,
                            ya::EWidgetAlignH::Center, ya::EWidgetAlignV::Center);
        }
    }

    bool handleInputEvent(const ya::Event& event, const ya::WidgetEventContext& ctx) override
    {
        const ya::EEvent::T eventType = event.getEventType();
        if (!ctx.bViaCapture && !hitTestLayoutRect(ctx.logicalPoint)) {
            return false;
        }
        switch (eventType) {
        case ya::EEvent::MouseButtonPressed:
            _bPressed    = true;
            _pressPoint  = ctx.logicalPoint;
            if (ya::WidgetTree* tree = getTree()) {
                tree->setPointerCapture(this);
            }
            return true;
        case ya::EEvent::MouseMoved:
            if (_bPressed && getTree() && !getTree()->isDragging()) {
                const float dist = glm::length(ctx.logicalPoint - _pressPoint);
                if (dist > 6.0f) {
                    ya::WidgetTree* tree = getTree();
                    tree->releasePointerCapture(this);
                    tree->beginDrag(this, _payload, _label);
                    _bPressed = false;
                }
            }
            return true;
        case ya::EEvent::MouseButtonReleased:
            _bPressed = false;
            if (ya::WidgetTree* tree = getTree()) {
                tree->releasePointerCapture(this);
            }
            return true;
        default:
            return false;
        }
    }

    void clearTransientInputState() override { _bPressed = false; }

  private:
    bool     _bPressed = false;
    glm::vec2 _pressPoint{};
};

/// Drop target: highlights during a valid hover, logs the drop.
struct FDemoDropZone : public ya::UIElement
{
    FDemoDropZone(std::string name) : ya::UIElement(std::move(name))
    {
        _hitFilter = ya::EWidgetHitFilter::Stop;
    }

    std::string _label;
    std::function<void(const std::string& payload)> _onDropped;

    bool canAcceptDrop(const std::string& payload, const glm::vec2&) override
    {
        return !payload.empty();
    }
    void onDrop(const std::string& payload, const glm::vec2&) override
    {
        _bHighlighted = false;
        invalidateProperty(ya::EUIPropertyImpact::Paint);
        if (_onDropped) {
            _onDropped(payload);
        }
    }
    void setDropHighlight(bool bHighlight) override
    {
        // The highlight is a paint attribute: without marking paint-dirty the
        // incremental paint cache keeps showing the pre-highlight draw items,
        // so the zone would never visibly light up (same contract as every
        // transient visual state in the framework).
        _bHighlighted = bHighlight;
        invalidateProperty(ya::EUIPropertyImpact::Paint);
    }

    void paintSelf(ya::UIFrameBuilder& builder) override
    {
        builder.addSprite(_layoutRect, _bHighlighted ? glm::vec4{0.24f, 0.46f, 0.82f, 0.85f}
                                                     : glm::vec4{0.13f, 0.15f, 0.19f, 1.0f},
                          nullptr);
        auto font = ya::FontManager::get()->getFont(ya::DEFAULT_RUNTIME_FONT_NAME, 13);
        if (font) {
            builder.addText(_layoutRect, _bHighlighted ? "DROP HERE" : _label,
                            {0.90f, 0.92f, 0.95f, 1.0f}, font,
                            ya::EWidgetAlignH::Center, ya::EWidgetAlignV::Center);
        }
    }

  private:
    bool _bHighlighted = false;
};

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
                               {0.90f, 0.45f, 0.70f, 1.0f}, 2.0f, 32);
    }
};

} // namespace

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("RenderDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("RenderForm");
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

        auto text = makeBodyText(label);
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

    state.renderProbeButton = makeDemoButton("RenderProbe", "Render Probe", 160.0f);
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
    auto panel = std::make_shared<ya::UIPanel>("WidgetsDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("WidgetsForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Widgets — buttons, checkbox, slider, combo box, image, text input"));

    // Button with counter.
    state.counterButton = makeDemoButton("Counter", std::format("Clicked {} times", state.clickCount), 180.0f);
    state.counterButton->_onClick = [&state, log]
    {
        ++state.clickCount;
        state.widgetLog = std::format("Button clicked (total {})", state.clickCount);
        log(state.widgetLog);
    };
    tree.attach(*form, state.counterButton);

    // Checkboxes.
    const auto addCheckBox = [&](const char* name, const std::string& label, bool& value)
    {
        auto check = std::make_shared<ya::UICheckBox>(name);
        check->_bAutoSize = true;
        check->_bChecked  = value;
        auto text = makeBodyText(label);
        check->addDetachedChild(text);
        check->_onChanged = [&value, log, name](bool bChecked)
        {
            value = bChecked;
            log(std::format("CheckBox '{}' -> {}", name, bChecked ? "on" : "off"));
        };
        tree.attach(*form, check);
        return check;
    };
    state.checkA = addCheckBox("CheckA", "Show grid lines", state.bCheckA);
    addCheckBox("CheckB", "Enable shadows", state.bCheckB);
    addCheckBox("CheckC", "VSync", state.bCheckC);

    // Slider.
    auto sliderRow = makeRow(tree, *form);
    tree.attach(*sliderRow, makeBodyText("Brightness"));
    state.slider = std::make_shared<ya::UISlider>("BrightnessSlider");
    state.slider->setSize({260.0f, 22.0f});
    state.slider->_value = state.sliderValue;
    state.slider->_onValueChanged = [&state, log](float value)
    {
        state.sliderValue = value;
        log(std::format("Slider -> {:.2f}", value));
    };
    tree.attach(*sliderRow, state.slider);

    // Combo box.
    auto comboRow = makeRow(tree, *form);
    tree.attach(*comboRow, makeBodyText("Render API"));
    state.combo = std::make_shared<ya::UIComboBox>("ApiCombo");
    state.combo->_items = {"Vulkan", "OpenGL", "Metal", "DirectX 12"};
    state.combo->setSize({180.0f, 26.0f});
    state.combo->_selectedIndex = std::clamp(state.comboIndex, 0, static_cast<int>(state.combo->_items.size()) - 1);
    state.combo->_onSelectionChanged = [&state, log](int index)
    {
        state.comboIndex = index;
        log(std::format("ComboBox -> {}", state.combo->currentLabel()));
    };
    tree.attach(*comboRow, state.combo);

    // Image (built-in textures through the host resolver).
    auto imageRow = makeRow(tree, *form);
    tree.attach(*imageRow, makeBodyText("Texture"));
    auto image = std::make_shared<ya::UIImage>("DemoImage");
    image->setSize({96.0f, 64.0f});
    image->_assetPath = "builtin/checkerboard";
    tree.attach(*imageRow, image);

    // Text field.
    auto fieldRow = makeRow(tree, *form);
    tree.attach(*fieldRow, makeBodyText("Notes"));
    auto field = std::make_shared<ya::UITextField>("NotesField");
    field->setSize({220.0f, 26.0f});
    field->_fontSize = 13;
    field->setText(state.textFieldValue);
    field->_onCommit = [&state, log](const std::string& text)
    {
        state.textFieldValue = text;
        log(std::format("TextField committed: '{}'", text));
    };
    tree.attach(*fieldRow, field);
}

void buildLayoutDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("LayoutDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("LayoutForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Layout — VBox / HBox, spacing, padding, alignment, stretch anchors"));
    tree.attach(*form, makeBodyText("Resize the window: containers stretch via anchorMin/anchorMax = {0,0}..{1,1}."));

    // HBox of three stretch cells.
    tree.attach(*form, makeLabel("HBox (horizontal container)"));
    auto hbox = std::make_shared<ya::UIContainer>("DemoHBox");
    hbox->setSize({0.0f, 64.0f});
    hbox->setDirection(ya::EWidgetBoxLayout::Horizontal);
    hbox->setSpacing(state.layoutSpacing);
    hbox->setPadding({4.0f, 4.0f});
    hbox->setClipChildren(true);
    tree.attach(*form, hbox);
    for (int i = 0; i < 3; ++i) {
        auto cell = std::make_shared<ya::UIPanel>(std::format("HCell{}", i));
        cell->setSize({380.0f, 50.0f});
        cell->setColor({0.22f + i * 0.06f, 0.30f + i * 0.04f, 0.38f, 1.0f});
        tree.attach(*hbox, cell);
        auto text = makeBodyText(std::format("Cell {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->setSize({0.0f, 0.0f});
        text->_hAlign    = ya::EWidgetAlignH::Center;
        text->_vAlign    = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    }

    // VBox with mixed content + center alignment.
    tree.attach(*form, makeLabel("VBox with End alignment"));
    auto vbox = std::make_shared<ya::UIContainer>("DemoVBox");
    vbox->setSize({0.0f, 140.0f});
    vbox->setDirection(ya::EWidgetBoxLayout::Vertical);
    vbox->setSpacing(6.0f);
    vbox->setPadding({6.0f, 6.0f});
    vbox->setMainAxisAlignment(ya::EWidgetMainAxisAlignment::End);
    vbox->setClipChildren(true);
    tree.attach(*form, vbox);
    for (int i = 0; i < 4; ++i) {
        auto cell = std::make_shared<ya::UIPanel>(std::format("VCell{}", i));
        cell->setColor({0.30f + i * 0.05f, 0.22f, 0.42f, 1.0f});
        tree.attach(*vbox, cell);
        auto text = makeBodyText(std::format("Row {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->setSize({0.0f, 0.0f});
        text->_hAlign    = ya::EWidgetAlignH::Center;
        text->_vAlign    = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    }

    // Spacing slider.
    auto spacingRow = makeRow(tree, *form);
    tree.attach(*spacingRow, makeBodyText("Spacing"));
    auto spacingSlider = std::make_shared<ya::UISlider>("SpacingSlider");
    spacingSlider->setSize({220.0f, 22.0f});
    spacingSlider->_value = state.layoutSpacing / 24.0f;
    const std::weak_ptr<ya::UIContainer> weakHBox = hbox;
    spacingSlider->_onValueChanged = [&state, log, weakHBox](float value)
    {
        state.layoutSpacing = value * 24.0f;
        if (const auto box = weakHBox.lock()) {
            box->setSpacing(state.layoutSpacing);
        }
        log(std::format("Spacing -> {:.1f}px", state.layoutSpacing));
    };
    tree.attach(*spacingRow, spacingSlider);
}

void buildMenusDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("MenusDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("MenusForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Menus & popups — the menu bar above opens popup menus"));
    tree.attach(*form, makeBodyText("Click a menu-bar entry, hover to switch, Esc or outside click closes."));

    // A button that opens a standalone popup menu.
    auto popupButton = makeDemoButton("PopupButton", "Open popup menu...", 180.0f);
    popupButton->_onClick = [&tree, &state, log]
    {
        auto menu = ya::UIMenu::create({
            {"New Document", [&state, log] { state.menuLog = "Menu: New Document"; log(state.menuLog); }},
            {"Open File...", [&state, log] { state.menuLog = "Menu: Open File..."; log(state.menuLog); }},
            {"Save", [&state, log] { state.menuLog = "Menu: Save"; log(state.menuLog); }},
            {"---", nullptr},
            {"Quit", [&state, log] { state.menuLog = "Menu: Quit"; log(state.menuLog); }},
        });
        menu->openAt(tree, {300.0f, 220.0f});
    };
    tree.attach(*form, popupButton);

    tree.attach(*form, makeBodyText("Keyboard: Up/Down move, Enter activates, Esc closes."));
}

void buildDragDropDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                       const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("DragDropDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("DragDropForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Drag & drop — press an item, drag onto the zone"));

    auto sourceRow = makeRow(tree, *form);
    const std::vector<std::string> payloads = {"asset.texture.diffuse", "asset.mesh.cube", "asset.material.pbr"};
    for (const std::string& payload : payloads) {
        auto item = std::make_shared<FDemoDragItem>("Drag_" + payload);
        item->setSize({160.0f, 30.0f});
        item->_payload = payload;
        item->_label   = payload;
        tree.attach(*sourceRow, item);
        if (payload == payloads[0]) {
            state.dragItem = item;
        }
    }

    auto zone = std::make_shared<FDemoDropZone>("DropZone");
    zone->_anchorMin = {0.0f, 0.0f};
    zone->_anchorMax = {1.0f, 0.0f};
    zone->setPosition({0.0f, 12.0f});
    zone->setSize({0.0f, 120.0f});
    zone->_label     = "Drop zone";
    zone->_onDropped = [&state, log](const std::string& payload)
    {
        state.dropLog = std::format("Dropped '{}'", payload);
        log(state.dropLog);
    };
    tree.attach(*form, zone);
    state.dropZone = zone;
}

void buildModalDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                    const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("ModalDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("ModalForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Popup dialog — a transparent shield swallows outside clicks, no dimming"));

    state.openModalButton = makeDemoButton("OpenModal", "Open dialog...", 180.0f);
    state.openModalButton->_onClick = [&tree, &state, log]
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
        dialog->setColor({0.16f, 0.18f, 0.22f, 1.0f});
        overlay->addDetachedChild(dialog);

        auto stack = std::make_shared<ya::UIContainer>("ModalStack");
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

        auto okButton = makeDemoButton("ModalOK", "OK", 80.0f);
        okButton->_onClick = [&state, overlay, nameField, log]
        {
            state.modalName = nameField->_text;
            log(std::format("Modal OK: '{}'", state.modalName));
            overlay->close();
        };
        buttons->addDetachedChild(okButton);

        auto cancelButton = makeDemoButton("ModalCancel", "Cancel", 80.0f);
        cancelButton->_onClick = [&state, overlay, log]
        {
            log("Modal cancelled");
            overlay->close();
        };
        buttons->addDetachedChild(cancelButton);

        overlay->_onDismiss = [&state]() { state.bModalOpen = false; };
        overlay->open(tree);
    };
    tree.attach(*form, state.openModalButton);

    tree.attach(*form, makeBodyText("Esc or clicking outside the dialog closes it; the page behind stays visible."));
}

void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("ScrollSplitDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    // Structural layout: one vertical container holds the header rows and
    // the split; the split is the stretch-last child that fills the remaining
    // content area, so no magic top padding / fixed header band is needed.
    auto layout = std::make_shared<ya::UIContainer>("ScrollSplitLayout");
    layout->_anchorMin        = {0.0f, 0.0f};
    layout->_anchorMax        = {1.0f, 1.0f};
    layout->setSize({0.0f, 0.0f});
    layout->setDirection(ya::EWidgetBoxLayout::Vertical);
    layout->setSpacing(10.0f);
    layout->setPadding({16.0f, 12.0f});
    tree.attach(*panel, layout);

    tree.attach(*layout, makeLabel("Scroll viewport + split pane — drag the divider"));
    tree.attach(*layout, makeBodyText("The split stretches with the window; hover the divider to grab it."));

    auto split = std::make_shared<ya::UISplitPane>("DemoSplit");
    split->setSize({0.0f, 0.0f});
    split->setSplitRatio(0.38f);
    split->setMinFirstExtent(120.0f);
    split->setMinSecondExtent(160.0f);
    tree.attach(*layout, split);
    if (auto* slot = layout->getBoxSlot(*split)) {
        slot->setSizeRule(ya::EUIBoxSlotSizeRule::Fill);
    }

    // Left: scrollable list.
    auto scroll = std::make_shared<ya::UIScrollViewport>("DemoScroll");
    tree.attach(*split, scroll);
    auto list = std::make_shared<ya::UIContainer>("DemoScrollList");
    list->_anchorMin = {0.0f, 0.0f};
    list->_anchorMax = {1.0f, 1.0f};
    list->setDirection(ya::EWidgetBoxLayout::Vertical);
    list->setSpacing(2.0f);
    list->setPadding({6.0f, 6.0f});
    tree.attach(*scroll, list);
    for (int i = 0; i < 24; ++i) {
        auto row = std::make_shared<ya::UIPanel>(std::format("ScrollRow{}", i));
        row->setSize({0.0f, 24.0f});
        row->setColor({0.18f + (i % 3) * 0.04f, 0.20f, 0.24f, 1.0f});
        tree.attach(*list, row);
        auto text = makeBodyText(std::format("Scrollable entry {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->setSize({0.0f, 0.0f});
        text->_vAlign    = ya::EWidgetAlignV::Center;
        text->setPosition({8.0f, 0.0f});
        tree.attach(*row, text);
    }

    // Right: colored pane.
    auto rightPane = std::make_shared<ya::UIPanel>("DemoSplitRight");
    rightPane->setColor({0.24f, 0.30f, 0.40f, 1.0f});
    tree.attach(*split, rightPane);
    auto rightText = makeBodyText("Drag the divider between panes\nWheel scrolls the list");
    rightText->_anchorMin = {0.0f, 0.0f};
    rightText->_anchorMax = {1.0f, 1.0f};
    rightText->setSize({0.0f, 0.0f});
    rightText->_hAlign    = ya::EWidgetAlignH::Center;
    rightText->_vAlign    = ya::EWidgetAlignV::Center;
    tree.attach(*rightPane, rightText);
}

void buildGalleryDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("GalleryDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    // The gallery content is taller than the viewport: wrap the form in a
    // scroll viewport so every section stays reachable and nothing paints
    // over the status bar.
    auto scroll = std::make_shared<ya::UIScrollViewport>("GalleryScroll");
    scroll->_anchorMin = {0.0f, 0.0f};
    scroll->_anchorMax = {1.0f, 1.0f};
    tree.attach(*panel, scroll);

    auto form = std::make_shared<ya::UIContainer>("GalleryForm");
    form->_bAutoSize = true;
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 0.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(12.0f);
    tree.attach(*scroll, form);

    // ---------------------------------------------------------------------
    // Section 1 — Reactive data binding (model -> view, no manual repaint).
    // Every widget below is driven by a Reactive<T>; mutating the ref via
    // set() marks only the dependent widgets dirty through the reactive
    // invalidation layer.
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("1. Reactive binding — model drives view"));

    // Counter: a Reactive<int> feeds a bound label; the button mutates the
    // ref, not the widget — the text re-paints automatically.
    auto counterRef = std::make_shared<ya::Reactive<int>>(0);
    auto boundCounter = std::make_shared<ya::UIText>("GalleryBoundCounter");
    boundCounter->_bAutoSize = true;
    boundCounter->_fontSize  = 14;
    auto counterStrRef = std::make_shared<ya::Reactive<std::string>>("Count: 0");
    boundCounter->bindText(counterStrRef);
    tree.attach(*form, boundCounter);

    auto incButton = makeDemoButton("GalleryInc", "Increment (reactive)", 200.0f);
    incButton->_onClick = [counterRef, counterStrRef, log]
    {
        const int next = counterRef->value() + 1;
        counterRef->set(next);
        counterStrRef->set(std::format("Count: {}", next));
        log(std::format("Reactive counter -> {}", next));
    };
    tree.attach(*form, incButton);

    // Enabled flag: a Reactive<bool> drives a second button's enabled state
    // through UIButton::bindEnabled (paint-dirty on change).
    auto enabledRef = std::make_shared<ya::Reactive<bool>>(true);
    auto dependentButton = makeDemoButton("GalleryDependent", "Enabled by reactive flag", 220.0f);
    dependentButton->bindEnabled(enabledRef);
    tree.attach(*form, dependentButton);

    auto toggleButton = makeDemoButton("GalleryToggle", "Toggle enabled flag", 220.0f);
    toggleButton->_onClick = [enabledRef, log]
    {
        const bool next = !enabledRef->value();
        enabledRef->set(next);
        log(std::format("Reactive enabled flag -> {}", next ? "on" : "off"));
    };
    tree.attach(*form, toggleButton);

    // Menu-bar item label bound to a Reactive<string> (UIMenuBarItem::bindLabel,
    // single-direction view <- model). Demonstrates the binding added for the
    // menu bar alongside the global shell menu.
    auto menuLabelRef = std::make_shared<ya::Reactive<std::string>>("Dynamic Item");
    auto localBar = std::make_shared<ya::UIMenuBar>("GalleryMenuBar");
    localBar->setSize({0.0f, 28.0f});
    auto dynItem = localBar->addItem("Dynamic Item", nullptr);
    dynItem->bindLabel(menuLabelRef);
    tree.attach(*form, localBar);

    auto renameButton = makeDemoButton("GalleryRename", "Rename menu item (reactive)", 260.0f);
    renameButton->_onClick = [menuLabelRef, log]
    {
        const std::string next = menuLabelRef->value() == "Dynamic Item" ? "Renamed!" : "Dynamic Item";
        menuLabelRef->set(next);
        log(std::format("Reactive menu label -> '{}'", next));
    };
    tree.attach(*form, renameButton);

    // Split ratio driven by a Reactive<float> (UIStyleSplitPane::bindSplitRatio).
    auto ratioRef = std::make_shared<ya::Reactive<float>>(0.45f);
    auto split = std::make_shared<ya::UISplitPane>("GallerySplit");
    split->setSize({0.0f, 120.0f});
    split->bindSplitRatio(ratioRef);
    split->setMinFirstExtent(80.0f);
    split->setMinSecondExtent(80.0f);
    tree.attach(*form, split);
    if (auto* slot = form->getBoxSlot(*split)) {
        slot->setSizeRule(ya::EUIBoxSlotSizeRule::Fill);
    }
    auto leftPane = std::make_shared<ya::UIPanel>("GallerySplitLeft");
    leftPane->setColor({0.20f, 0.24f, 0.32f, 1.0f});
    auto rightPane2 = std::make_shared<ya::UIPanel>("GallerySplitRight");
    rightPane2->setColor({0.28f, 0.22f, 0.32f, 1.0f});
    // Attach panes to the split first so they belong to the tree; only then
    // can their children be attached. Otherwise WidgetTree::attach rejects
    // the child with "parent does not belong to this tree".
    tree.attach(*split, leftPane);
    tree.attach(*split, rightPane2);

    auto leftText = makeBodyText("ratio <- reactive");
    leftText->_anchorMin = {0.0f, 0.0f};
    leftText->_anchorMax = {1.0f, 1.0f};
    leftText->setSize({0.0f, 0.0f});
    leftText->_hAlign = ya::EWidgetAlignH::Center;
    leftText->_vAlign = ya::EWidgetAlignV::Center;
    tree.attach(*leftPane, leftText);

    auto rightText2 = makeBodyText("drag divider");
    rightText2->_anchorMin = {0.0f, 0.0f};
    rightText2->_anchorMax = {1.0f, 1.0f};
    rightText2->setSize({0.0f, 0.0f});
    rightText2->_hAlign = ya::EWidgetAlignH::Center;
    rightText2->_vAlign = ya::EWidgetAlignV::Center;
    tree.attach(*rightPane2, rightText2);

    auto ratioButton = makeDemoButton("GalleryRatio", "Set ratio 0.25 (reactive)", 240.0f);
    ratioButton->_onClick = [ratioRef, log]
    {
        ratioRef->set(0.25f);
        log("Reactive split ratio -> 0.25");
    };
    tree.attach(*form, ratioButton);

    // ---------------------------------------------------------------------
    // Section 2 — TreeView (data-driven widget) + selection as a reactive
    // source. The selected node id is a Reactive<string> that a bound label
    // subscribes to: selecting a row updates the label with no manual wiring.
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("2. TreeView (data-driven widget) + reactive selection"));

    auto roots = std::make_shared<ya::ReactiveList<ya::UITreeView::FNode>>();
    roots->push({"root", "Scene Root", {
        {"mesh", "Mesh", {}},
        {"light", "Light", {
            {"point", "Point Light", {}},
            {"spot", "Spot Light", {}},
        }},
        {"camera", "Camera", {}},
    }});
    roots->push({"ui", "UI", {
        {"hud", "HUD", {}},
        {"menu", "Menu", {}},
    }});

    auto treeView = std::make_shared<ya::UITreeView>("GalleryTree");
    // AutoSize: the tree grows/shrinks with the expanded-row count, so
    // expanding never overflows the widget rect (the framework also clips
    // the tree's own paint to its rect as a backstop).
    treeView->_bAutoSize = true;
    treeView->setSize({0.0f, 0.0f});
    treeView->bindData(roots);
    treeView->setExpanded("root", true);
    treeView->_bReorderable = true;
    treeView->_onReorder = [roots, log](const std::string& fromId, const std::string& toId, int mode)
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
        // Find and remove `from` at the root level.
        ya::UITreeView::FNode moved;
        bool bFound = false;
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
        // Locate the target index in the rebuilt list.
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
            // Refuse to drop a node into its own descendant (would create
            // a cycle: the tree flattens recursively and would blow the
            // stack — the framework caps depth as a backstop, the host
            // must validate its own data).
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
            // Into: append as the target's child.
            rebuilt[static_cast<size_t>(targetIndex)].children.push_back(moved);
        }
        else {
            const int insertIndex = targetIndex + (mode == 2 ? 1 : 0);
            rebuilt.insert(rebuilt.begin() + insertIndex, moved);
        }
        // Rebuild the reactive list in place (clear + push notifies dependents).
        roots->clear();
        for (const auto& n : rebuilt) {
            roots->push(n);
        }
        log(std::format("Tree reorder '{}' {} '{}'", fromId, mode == 0 ? "before" : (mode == 1 ? "into" : "after"), toId));
    };
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
    tree.attach(*form, treeView);

    // Filter box drives the tree's visible set (Reactive<string>).
    auto filterRow = makeRow(tree, *form);
    tree.attach(*filterRow, makeBodyText("Filter"));
    auto filterField = std::make_shared<ya::UITextField>("GalleryTreeFilter");
    filterField->setSize({160.0f, 24.0f});
    filterField->_fontSize = 13;
    filterField->_onTextChanged = [treeFilterRef](const std::string& text)
    {
        treeFilterRef->set(text);
    };
    tree.attach(*filterRow, filterField);

    // Selected id mirror: a bound label subscribes to the tree's selection ref.
    auto selectedLabel = std::make_shared<ya::UIText>("GallerySelected");
    selectedLabel->_bAutoSize = true;
    selectedLabel->_fontSize  = 13;
    auto selStrRef = std::make_shared<ya::Reactive<std::string>>("(none)");
    // Mirror the tree's selection into a string ref the label binds to, so
    // the label re-paints through the reactive layer on every selection.
    selectedLabel->bindText(selStrRef);
    treeView->_onSelectionChanged = [selStrRef, log](const std::string& id)
    {
        selStrRef->set(id.empty() ? "(none)" : id);
        log(std::format("Tree selection -> '{}'", id));
    };
    tree.attach(*form, selectedLabel);

    // ---------------------------------------------------------------------
    // Section 3 — Style system. A UIStyleSet holds named styles; a style is
    // a Reactive<FWidgetStyle>, so mutating it via set() repaints every
    // widget bound to it without touching per-widget color fields.
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("3. Style system — one edit restyles the group"));

    auto styleSet = std::make_shared<ya::UIStyleSet>();
    // Define the theme ONCE and mutate the returned handle with set().
    // Re-defining the same name would replace the handle and orphan every
    // binding that captured the previous one (they would never repaint).
    const ya::FWidgetStyle kDarkTheme{
        .fillColor = {0.16f, 0.18f, 0.22f, 1.0f},
        .textColor = {0.82f, 0.86f, 0.92f, 1.0f},
        .fontSize  = 14,
        .padding   = {8.0f, 4.0f},
    };
    const ya::FWidgetStyle kWhiteTheme{
        .fillColor = {0.94f, 0.95f, 0.97f, 1.0f},
        .textColor = {0.10f, 0.12f, 0.16f, 1.0f},
        .fontSize  = 14,
        .padding   = {8.0f, 4.0f},
    };
    auto themeRef = styleSet->define("theme", kDarkTheme);

    // Buttons outlive this builder function: capture only values/shared_ptrs.
    // A [&] capture (styleSet / local bool / local FWidgetStyle) would dangle
    // after the page is built and crash on the first click.
    auto bDarkRef = std::make_shared<bool>(true);
    const auto applyTheme = [themeRef, kDarkTheme, kWhiteTheme](bool bDark)
    {
        themeRef->set(bDark ? kDarkTheme : kWhiteTheme);
    };

    auto styledText = std::make_shared<ya::UIText>("GalleryStyledText");
    styledText->_bAutoSize = true;
    styledText->setText("Styled text (themed)");
    styledText->_bFillBackground = true;
    styledText->bindStyle(themeRef);
    tree.attach(*form, styledText);

    auto styledCaption = std::make_shared<ya::UIText>("GalleryStyledCaption");
    styledCaption->_bAutoSize = true;
    styledCaption->setText("Another themed text bound to the same style");
    styledCaption->_bFillBackground = true;
    styledCaption->bindStyle(themeRef);
    tree.attach(*form, styledCaption);

    auto themeButton = makeDemoButton("GalleryTheme", "Toggle theme (dark/white)", 260.0f);
    themeButton->_onClick = [applyTheme, bDarkRef, log]
    {
        *bDarkRef = !*bDarkRef;
        applyTheme(*bDarkRef);
        log(std::format("Style theme -> {}", *bDarkRef ? "dark" : "white"));
    };
    tree.attach(*form, themeButton);

    // ---------------------------------------------------------------------
    // Section 4 — Vector primitives (UIFrameBuilder addLine / addRectOutline
    // / addBezierCubic). The building blocks for RenderGraph topology wires
    // and drag-insert highlight lines in the editor.
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("4. Vector primitives — lines, outline, bezier"));

    auto vectorCanvas = std::make_shared<FVectorDemoCanvas>("GalleryVectorCanvas");
    vectorCanvas->setSize({430.0f, 110.0f});
    tree.attach(*form, vectorCanvas);
    // Keep the canvas at its fixed width (box cross-axis default is Stretch).
    if (auto* slot = form->getBoxSlot(*vectorCanvas)) {
        slot->setCrossAlignment(ya::EUIBoxSlotCrossAlignment::Start);
    }

    // ---------------------------------------------------------------------
    // Section 5 — Table/Grid (data-driven UITableGrid: reactive row source +
    // reactive selection, grid separators drawn via the vector primitives).
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("5. Table — data-driven grid with reactive selection"));

    auto tableRows = std::make_shared<ya::ReactiveList<ya::UITableGrid::FTableRow>>();
    tableRows->push({"hdr", {"Name", "Type", "Count", "Visible"}});
    tableRows->push({"r1", {"Cube", "StaticMesh", "3", "yes"}});
    tableRows->push({"r2", {"PointLight", "Light", "2", "yes"}});
    tableRows->push({"r3", {"Camera", "Node", "1", "no"}});
    tableRows->push({"r4", {"Material", "Asset", "8", "yes"}});

    auto tableGrid = std::make_shared<ya::UITableGrid>("GalleryTableGrid");
    tableGrid->setSize({400.0f, 0.0f});
    tableGrid->_bAutoSize = true;
    tableGrid->_columnWidths = {140.0f, 100.0f, 0.0f, 0.0f}; // last two stretch
    tableGrid->bindData(tableRows);
    auto tableSelRef = std::make_shared<ya::Reactive<int>>(1);
    tableGrid->bindSelection(tableSelRef);
    tableGrid->_onSelectionChanged = [log](int row)
    {
        log(std::format("Table row -> {}", row));
    };
    tree.attach(*form, tableGrid);
    // Keep the grid at its fixed width (box cross-axis default is Stretch).
    if (auto* slot = form->getBoxSlot(*tableGrid)) {
        slot->setCrossAlignment(ya::EUIBoxSlotCrossAlignment::Start);
    }

    // A cell can hold an arbitrary widget: put a button into row 3 / column
    // 2 (it replaces the text cell and paints itself on top of the grid).
    auto cellButton = makeDemoButton("GalleryCellButton", "Inspect", 0.0f);
    tree.attach(*tableGrid, cellButton);
    if (auto* cellSlot = tableGrid->getCellSlot(*cellButton)) {
        cellSlot->setCell(3, 2);
    }

    auto tableCaption = makeBodyText("Click a row to select (reactive selection ref); row 3 / col 2 holds a real button widget.");
    tree.attach(*form, tableCaption);

    // ---------------------------------------------------------------------
    // Section 6 — Input controls (DragFloat / SpinBox / RadioButton /
    // ColorEdit / SearchComboBox — the editor's ManipulateSpec-driven
    // property editors need these).
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("6. Input controls — drag, spin, radio, color, search combo"));

    auto dragRow = makeRow(tree, *form);
    tree.attach(*dragRow, makeBodyText("DragFloat"));
    auto dragFloat = std::make_shared<ya::UIDragFloat>("GalleryDragFloat");
    dragFloat->setSize({120.0f, 24.0f});
    dragFloat->_value = 3.5f;
    dragFloat->_onValueChanged = [log](float v) { log(std::format("DragFloat -> {:.2f}", v)); };
    tree.attach(*dragRow, dragFloat);

    auto spinRow = makeRow(tree, *form);
    tree.attach(*spinRow, makeBodyText("SpinBox"));
    auto spinBox = std::make_shared<ya::UISpinBox>("GallerySpinBox");
    spinBox->setSize({120.0f, 24.0f});
    spinBox->_value = 8.0f;
    spinBox->_step  = 1.0f;
    spinBox->_onValueChanged = [log](float v) { log(std::format("SpinBox -> {:.2f}", v)); };
    tree.attach(*spinRow, spinBox);

    auto radioRow = makeRow(tree, *form);
    tree.attach(*radioRow, makeBodyText("Radio"));
    auto groupValue = std::make_shared<int>(0);
    auto radios = std::make_shared<std::vector<std::shared_ptr<ya::UIRadioButton>>>();
    const std::vector<std::string> radioLabels = {"Shadow", "CSM", "None"};
    for (size_t i = 0; i < radioLabels.size(); ++i) {
        auto radio = std::make_shared<ya::UIRadioButton>(std::format("GalleryRadio{}", i));
        radio->setSize({90.0f, 22.0f});
        radio->_label     = radioLabels[i];
        radio->_bChecked  = static_cast<int>(i) == *groupValue;
        // Capture shared_ptr by value: the button outlives this builder.
        radio->_onSelect = [radios, log, label = radioLabels[i]](ya::UIRadioButton* self)
        {
            for (auto& r : *radios) {
                r->setChecked(r.get() == self);
            }
            log(std::format("Radio -> {}", label));
        };
        radios->push_back(radio);
        tree.attach(*radioRow, radio);
    }

    auto colorRow = makeRow(tree, *form);
    tree.attach(*colorRow, makeBodyText("ColorEdit"));
    auto colorEdit = std::make_shared<ya::UIColorEdit>("GalleryColorEdit");
    colorEdit->setSize({170.0f, 28.0f});
    colorEdit->_color = {0.24f, 0.46f, 0.82f, 1.0f};
    colorEdit->_onColorChanged = [log](const glm::vec4& c)
    {
        log(std::format("Color -> ({:.2f}, {:.2f}, {:.2f}, {:.2f})", c.r, c.g, c.b, c.a));
    };
    tree.attach(*colorRow, colorEdit);

    auto searchRow = makeRow(tree, *form);
    tree.attach(*searchRow, makeBodyText("SearchCombo"));
    auto searchCombo = std::make_shared<ya::UISearchComboBox>("GallerySearchCombo");
    searchCombo->setSize({180.0f, 24.0f});
    searchCombo->_items = {"Cube", "Sphere", "Capsule", "Plane", "Cone", "Torus"};
    searchCombo->_selectedIndex = 0;
    searchCombo->_onSelectionChanged = [log](int index) { log(std::format("SearchCombo -> {}", index)); };
    tree.attach(*searchRow, searchCombo);

    // ---------------------------------------------------------------------
    // Section 7 — Drag & drop (UIDragSource / UIDropTarget wrappers over the
    // WidgetTree drag session; the drop highlight uses the P1 outline).
    // ---------------------------------------------------------------------
    tree.attach(*form, makeLabel("7. Drag & drop — sources onto targets"));

    auto dragDemoRow = makeRow(tree, *form);
    for (int i = 0; i < 3; ++i) {
        auto source = std::make_shared<ya::UIDragSource>(std::format("GalleryDragSrc{}", i));
        source->setSize({110.0f, 26.0f});
        source->_label = std::format("Item {}", i + 1);
        source->_makePayload = [i] { return std::format("payload.{}", i + 1); };
        tree.attach(*dragDemoRow, source);
    }

    auto dropRow = makeRow(tree, *form);
    auto dropResult = std::make_shared<ya::Reactive<std::string>>("(drop something here)");
    auto dropLabel = std::make_shared<ya::UIText>("GalleryDropResult");
    dropLabel->_bAutoSize = true;
    dropLabel->_fontSize = 13;
    dropLabel->bindText(dropResult);
    tree.attach(*dropRow, dropLabel);

    auto dropZoneA = std::make_shared<ya::UIDropTarget>("GalleryDropA");
    dropZoneA->setSize({180.0f, 60.0f});
    dropZoneA->_label = "Zone A: accepts any";
    dropZoneA->_accept = [](const std::string&) { return true; };
    dropZoneA->_onDrop = [dropResult, log](const std::string& payload, const glm::vec2&)
    {
        dropResult->set(std::format("Zone A <- {}", payload));
        log(std::format("Dropped '{}' on Zone A", payload));
    };
    tree.attach(*dropRow, dropZoneA);

    auto dropZoneB = std::make_shared<ya::UIDropTarget>("GalleryDropB");
    dropZoneB->setSize({180.0f, 60.0f});
    dropZoneB->_label = "Zone B: only payload.2";
    // Zone B demonstrates the accept predicate: it only accepts payload.2
    // (the hover highlight appears only for the matching item).
    dropZoneB->_accept = [](const std::string& payload) { return payload == "payload.2"; };
    dropZoneB->_onDrop = [dropResult, log](const std::string& payload, const glm::vec2&)
    {
        dropResult->set(std::format("Zone B <- {}", payload));
        log(std::format("Dropped '{}' on Zone B", payload));
    };
    tree.attach(*dropRow, dropZoneB);

    tree.attach(*form, makeBodyText("Expected: incrementing, toggling, renaming, resizing, selecting and theming all update their targets without the page rebuilding."));
}



void buildInteractionsDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                           const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("InteractionsDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->setColor(kPanelColor);
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("InteractionsForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->setSize({0.0f, 0.0f});
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(12.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Tooltip & wrapped text (editor-parity P6)"));

    auto tipRow = makeRow(tree, *form);
    auto tooltipButton = makeDemoButton("TooltipBtn", "Hover me (tooltip)", 200.0f);
    tooltipButton->setTooltip("This tooltip appears after a 0.5s hover dwell.");
    tree.attach(*tipRow, tooltipButton);

    auto wrappedText = std::make_shared<ya::UIText>("WrappedText");
    wrappedText->_bWrap = true;
    wrappedText->_maxWrapWidth = 360.0f;
    wrappedText->_bAutoSize = true;
    wrappedText->_fontSize = 13;
    wrappedText->setText(
        "This paragraph demonstrates automatic text wrapping. Long content breaks onto "
        "multiple lines instead of overflowing its box, matching the editor's TextWrapped "
        "behavior. CJK text also wraps: 中文换行测试中文换行测试。");
    tree.attach(*form, wrappedText);
    if (auto* slot = form->getBoxSlot(*wrappedText)) {
        slot->setCrossAlignment(ya::EUIBoxSlotCrossAlignment::Start);
    }

    // Subtree disable: the whole group goes input-inert when disabled.
    tree.attach(*form, makeLabel("Subtree disable"));
    auto group = std::make_shared<ya::UIContainer>("DisableGroup");
    group->setDirection(ya::EWidgetBoxLayout::Horizontal);
    group->setSpacing(8.0f);
    tree.attach(*form, group);
    auto groupBtnA = makeDemoButton("GroupBtnA", "Group button A", 140.0f);
    groupBtnA->_onClick = [log] { log("Group button A clicked"); };
    tree.attach(*group, groupBtnA);
    auto groupBtnB = makeDemoButton("GroupBtnB", "Group button B", 140.0f);
    groupBtnB->_onClick = [log] { log("Group button B clicked"); };
    tree.attach(*group, groupBtnB);

    auto bGroupEnabled = std::make_shared<bool>(true);
    auto toggleBtn = makeDemoButton("ToggleGroupBtn", "Toggle group enabled", 200.0f);
    toggleBtn->_onClick = [group, bGroupEnabled, log]
    {
        *bGroupEnabled = !*bGroupEnabled;
        group->setEnabled(*bGroupEnabled);
        const std::string groupState = *bGroupEnabled ? "enabled" : "disabled";
        log(std::format("Group {}", groupState));
    };
    tree.attach(*form, toggleBtn);

    // Modal dialog.
    tree.attach(*form, makeLabel("Modal dialog"));
    auto openDialogBtn = makeDemoButton("OpenDialogBtn", "Open dialog...", 180.0f);
    openDialogBtn->_onClick = [&tree, log]
    {
        auto content = std::make_shared<ya::UIText>("DialogContent");
        content->_bAutoSize = true;
        content->_fontSize  = 13;
        content->_color     = {0.88f, 0.90f, 0.94f, 1.0f};
        content->setText("This is a modal dialog built on UIPopupOverlay's modal role.");
        auto dialog = ya::UIDialog::create("Confirm", content);
        dialog->_onClosed = [log](bool bConfirmed)
        {
            const std::string result = bConfirmed ? "confirmed" : "cancelled";
            log(std::format("Dialog closed: {}", result));
        };
        dialog->open(tree);
    };
    tree.attach(*form, openDialogBtn);
}



void buildDockDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                   const std::function<void(const std::string&)>& log)
{
    auto dock = std::make_shared<ya::UIDockSpace>("DemoDock");
    dock->_anchorMin = {0.0f, 0.0f};
    dock->_anchorMax = {1.0f, 1.0f};
    tree.attach(parent, dock);

    const auto makePanel = [](const std::string& name, const std::string& text)
    {
        auto panel = std::make_shared<ya::UIPanel>(name + "_Body");
        panel->setColor({0.12f, 0.13f, 0.17f, 1.0f});
        auto label = std::make_shared<ya::UIText>(name + "_Label");
        label->_hAlign = ya::EWidgetAlignH::Center;
        label->_vAlign = ya::EWidgetAlignV::Center;
        label->_fontSize = 14;
        label->setText(text);
        panel->addDetachedChild(label);
        return std::shared_ptr<ya::UIElement>(panel);
    };

    dock->addPanel("Scene", makePanel("Scene", "Scene viewport"));
    dock->addPanel("Inspector", makePanel("Inspector", "Inspector panel"));
    dock->addPanel("Console", makePanel("Console", "Console output"));

    (void)state;
    (void)log;
}

} // namespace guiworkbench
