#include "WorkbenchDemoPages.h"

#include "Core/Log.h"

#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/WidgetTree.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/MenuBar.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/PopupOverlay.h"
#include "GUI/Widgets/Controls/ScrollViewport.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SplitPane.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/Controls/TextField.h"

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
    label->_text      = text;
    label->_color     = kHeaderColor;
    return label;
}

std::shared_ptr<ya::UIText> makeBodyText(const std::string& text)
{
    auto label = std::make_shared<ya::UIText>(text + "_Body");
    label->_bAutoSize = true;
    label->_fontSize  = 13;
    label->_text      = text;
    label->_color     = kTextColor;
    return label;
}

std::shared_ptr<ya::UIButton> makeDemoButton(const std::string& name, const std::string& label, float width = 0.0f)
{
    auto button = std::make_shared<ya::UIButton>(name);
    if (width > 0.0f) {
        button->_size = {width, 26.0f};
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
    text->_text      = label;
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
        if (_onDropped) {
            _onDropped(payload);
        }
    }
    void setDropHighlight(bool bHighlight) override { _bHighlighted = bHighlight; }

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

} // namespace

void buildRenderDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("RenderDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("RenderForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Render — correctness baseline (text, button, image, edge markers)"));
    tree.attach(*form, makeBodyText("Use this page as the first-frame render sanity target before deeper layout/event refactors."));

    auto markerRow = std::make_shared<ya::UIContainer>("RenderMarkers");
    markerRow->setDirection(ya::EWidgetBoxLayout::Horizontal);
    markerRow->setSpacing(8.0f);
    markerRow->_size      = {0.0f, 84.0f};
    tree.attach(*form, markerRow);

    const auto addMarker = [&](const std::string& name, const std::string& label, const glm::vec4& color)
    {
        auto cell = std::make_shared<ya::UIPanel>(name);
        cell->_size  = {180.0f, 84.0f};
        cell->_color = color;
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
    image->_size      = {128.0f, 96.0f};
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
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("WidgetsForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
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
    state.slider->_size  = {260.0f, 22.0f};
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
    state.combo->_size  = {180.0f, 26.0f};
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
    image->_size      = {96.0f, 64.0f};
    image->_assetPath = "builtin/checkerboard";
    tree.attach(*imageRow, image);

    // Text field.
    auto fieldRow = makeRow(tree, *form);
    tree.attach(*fieldRow, makeBodyText("Notes"));
    auto field = std::make_shared<ya::UITextField>("NotesField");
    field->_size     = {220.0f, 26.0f};
    field->_fontSize = 13;
    field->_text     = state.textFieldValue;
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
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("LayoutForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Layout — VBox / HBox, spacing, padding, alignment, stretch anchors"));
    tree.attach(*form, makeBodyText("Resize the window: containers stretch via anchorMin/anchorMax = {0,0}..{1,1}."));

    // HBox of three stretch cells.
    tree.attach(*form, makeLabel("HBox (horizontal container)"));
    auto hbox = std::make_shared<ya::UIContainer>("DemoHBox");
    hbox->_size      = {0.0f, 64.0f};
    hbox->setDirection(ya::EWidgetBoxLayout::Horizontal);
    hbox->setSpacing(state.layoutSpacing);
    hbox->setPadding({4.0f, 4.0f});
    hbox->setClipChildren(true);
    tree.attach(*form, hbox);
    for (int i = 0; i < 3; ++i) {
        auto cell = std::make_shared<ya::UIPanel>(std::format("HCell{}", i));
        cell->_size  = {380.0f, 50.0f};
        cell->_color = {0.22f + i * 0.06f, 0.30f + i * 0.04f, 0.38f, 1.0f};
        tree.attach(*hbox, cell);
        auto text = makeBodyText(std::format("Cell {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->_size      = {0.0f, 0.0f};
        text->_hAlign    = ya::EWidgetAlignH::Center;
        text->_vAlign    = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    }

    // VBox with mixed content + center alignment.
    tree.attach(*form, makeLabel("VBox with End alignment"));
    auto vbox = std::make_shared<ya::UIContainer>("DemoVBox");
    vbox->_size       = {0.0f, 140.0f};
    vbox->setDirection(ya::EWidgetBoxLayout::Vertical);
    vbox->setSpacing(6.0f);
    vbox->setPadding({6.0f, 6.0f});
    vbox->setMainAxisAlignment(ya::EWidgetMainAxisAlignment::End);
    vbox->setClipChildren(true);
    tree.attach(*form, vbox);
    for (int i = 0; i < 4; ++i) {
        auto cell = std::make_shared<ya::UIPanel>(std::format("VCell{}", i));
        cell->_color = {0.30f + i * 0.05f, 0.22f, 0.42f, 1.0f};
        tree.attach(*vbox, cell);
        auto text = makeBodyText(std::format("Row {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->_size      = {0.0f, 0.0f};
        text->_hAlign    = ya::EWidgetAlignH::Center;
        text->_vAlign    = ya::EWidgetAlignV::Center;
        tree.attach(*cell, text);
    }

    // Spacing slider.
    auto spacingRow = makeRow(tree, *form);
    tree.attach(*spacingRow, makeBodyText("Spacing"));
    auto spacingSlider = std::make_shared<ya::UISlider>("SpacingSlider");
    spacingSlider->_size  = {220.0f, 22.0f};
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
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("MenusForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
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
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("DragDropForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Drag & drop — press an item, drag onto the zone"));

    auto sourceRow = makeRow(tree, *form);
    const std::vector<std::string> payloads = {"asset.texture.diffuse", "asset.mesh.cube", "asset.material.pbr"};
    for (const std::string& payload : payloads) {
        auto item = std::make_shared<FDemoDragItem>("Drag_" + payload);
        item->_size    = {160.0f, 30.0f};
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
    zone->_position  = {0.0f, 12.0f};
    zone->_size      = {0.0f, 120.0f};
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
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    auto form = std::make_shared<ya::UIContainer>("ModalForm");
    form->_anchorMin = {0.0f, 0.0f};
    form->_anchorMax = {1.0f, 1.0f};
    form->setPadding({16.0f, 12.0f});
    form->_size      = {0.0f, 0.0f};
    form->setDirection(ya::EWidgetBoxLayout::Vertical);
    form->setSpacing(10.0f);
    tree.attach(*panel, form);

    tree.attach(*form, makeLabel("Modal — a dimming overlay blocks the whole app until dismissed"));

    state.openModalButton = makeDemoButton("OpenModal", "Open modal dialog...", 180.0f);
    state.openModalButton->_onClick = [&tree, &state, log]
    {
        if (state.bModalOpen) {
            return;
        }
        state.bModalOpen = true;

        auto overlay = std::make_shared<ya::UIPopupOverlay>("ModalOverlay");
        overlay->_bModal     = true;
        overlay->_contentPos = {440.0f, 300.0f};

        auto dialog = std::make_shared<ya::UIPanel>("ModalDialog");
        dialog->_size  = {360.0f, 170.0f};
        dialog->_color = {0.16f, 0.18f, 0.22f, 1.0f};
        overlay->addDetachedChild(dialog);

        auto stack = std::make_shared<ya::UIContainer>("ModalStack");
        stack->_anchorMin = {0.0f, 0.0f};
        stack->_anchorMax = {1.0f, 1.0f};
        stack->_position  = {16.0f, 14.0f};
        stack->_size      = {0.0f, 0.0f};
        stack->setDirection(ya::EWidgetBoxLayout::Vertical);
        stack->setSpacing(12.0f);
        dialog->addDetachedChild(stack);

        auto title = makeLabel("About / New Project", 14.0f);
        stack->addDetachedChild(title);

        auto nameField = std::make_shared<ya::UITextField>("ModalName");
        nameField->_size     = {320.0f, 26.0f};
        nameField->_fontSize = 13;
        nameField->_text     = state.modalName;
        stack->addDetachedChild(nameField);

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

    tree.attach(*form, makeBodyText("Esc or clicking the dimmed area closes the dialog."));
}

void buildScrollSplitDemo(ya::WidgetTree& tree, ya::UIElement& parent, FDemoState& state,
                          const std::function<void(const std::string&)>& log)
{
    auto panel = std::make_shared<ya::UIPanel>("ScrollSplitDemo");
    panel->_anchorMin = {0.0f, 0.0f};
    panel->_anchorMax = {1.0f, 1.0f};
    panel->_color     = kPanelColor;
    tree.attach(parent, panel);

    // Structural layout: one vertical container holds the header rows and
    // the split; the split is the stretch-last child that fills the remaining
    // content area, so no magic top padding / fixed header band is needed.
    auto layout = std::make_shared<ya::UIContainer>("ScrollSplitLayout");
    layout->_anchorMin        = {0.0f, 0.0f};
    layout->_anchorMax        = {1.0f, 1.0f};
    layout->_size             = {0.0f, 0.0f};
    layout->setDirection(ya::EWidgetBoxLayout::Vertical);
    layout->setSpacing(10.0f);
    layout->setPadding({16.0f, 12.0f});
    tree.attach(*panel, layout);

    tree.attach(*layout, makeLabel("Scroll viewport + split pane — drag the divider"));
    tree.attach(*layout, makeBodyText("The split stretches with the window; hover the divider to grab it."));

    auto split = std::make_shared<ya::UISplitPane>("DemoSplit");
    split->_size            = {0.0f, 0.0f};
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
        row->_size  = {0.0f, 24.0f};
        row->_color = {0.18f + (i % 3) * 0.04f, 0.20f, 0.24f, 1.0f};
        tree.attach(*list, row);
        auto text = makeBodyText(std::format("Scrollable entry {}", i + 1));
        text->_anchorMin = {0.0f, 0.0f};
        text->_anchorMax = {1.0f, 1.0f};
        text->_size      = {0.0f, 0.0f};
        text->_vAlign    = ya::EWidgetAlignV::Center;
        text->_position  = {8.0f, 0.0f};
        tree.attach(*row, text);
    }

    // Right: colored pane.
    auto rightPane = std::make_shared<ya::UIPanel>("DemoSplitRight");
    rightPane->_color = {0.24f, 0.30f, 0.40f, 1.0f};
    tree.attach(*split, rightPane);
    auto rightText = makeBodyText("Drag the divider between panes\nWheel scrolls the list");
    rightText->_anchorMin = {0.0f, 0.0f};
    rightText->_anchorMax = {1.0f, 1.0f};
    rightText->_size      = {0.0f, 0.0f};
    rightText->_hAlign    = ya::EWidgetAlignH::Center;
    rightText->_vAlign    = ya::EWidgetAlignV::Center;
    tree.attach(*rightPane, rightText);
}

} // namespace guiworkbench
