#include "../WorkbenchDemoPages.h"
#include "DemoPageCommon.h"

#include "GUI/Declarative/Build.h"
#include "GUI/Binding/Reactive.h"
#include "GUI/Widgets/Controls/CheckBox.h"
#include "GUI/Widgets/Controls/ColorEdit.h"
#include "GUI/Widgets/Controls/ComboBox.h"
#include "GUI/Widgets/Controls/DragFloat.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/RadioButton.h"
#include "GUI/Widgets/Controls/SearchComboBox.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Slider.h"
#include "GUI/Widgets/Controls/SpinBox.h"
#include "GUI/Widgets/Controls/TabBar.h"
#include "GUI/Widgets/Controls/TextField.h"

#include <algorithm>
#include <format>
#include <memory>
#include <vector>

namespace guiworkbench
{

void buildWidgetsDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                      const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };

    auto counter = ya::ui::button("Counter")
                       .child(ya::ui::text("Counter_Label")
                                  .setText(std::format("Clicked {} times", state.clickCount))
                                  .setFontSize(13)
                                  .setHAlign(ya::EWidgetAlignH::Center)
                                  .setVAlign(ya::EWidgetAlignV::Center))
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
                      .setValue(state.sliderValue)
                      .setOnValueChanged([&state, log](float value)
                                         {
                          state.sliderValue = value;
                          log(std::format("Slider -> {:.2f}", value)); });
    state.slider = slider.share();

    auto combo = ya::ui::comboBox("ApiCombo")
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

    auto selectedId = std::make_shared<std::string>("alpha");
    auto rows       = std::make_shared<std::vector<std::shared_ptr<ya::UISelectableRow>>>();
    auto makeRow    = [selectedId, log, rows](std::string name, std::string id, const std::string& label)
    {
        auto row = ya::ui::selectableRow(std::move(name))
            .setItemId(id)
            .setSelected(*selectedId == id)
            .setOnSelect([selectedId, log, rows](const std::string& itemId)
                         {
                             *selectedId = itemId;
                             for (auto& item : *rows) {
                                 if (item) {
                                     item->setSelected(item->_itemId == itemId);
                                 }
                             }
                             log(std::format("SelectableRow -> {}", itemId));
                         })
            .child(ya::ui::text(id + "_Label").setText(label).setFontSize(13).setVAlign(ya::EWidgetAlignV::Center));
        rows->push_back(row.share());
        return row;
    };

    auto tabSelected = std::make_shared<ya::Reactive<std::string>>("Selected tab: One");
    auto tabBar      = std::make_shared<ya::UITabBar>("WidgetTabs");
    tabBar->addTab("One");
    tabBar->addTab("Two");
    tabBar->addTab("Three");
    tabBar->_onTabSelected = [tabSelected, log](int index)
    {
        const char* labels[] = {"One", "Two", "Three"};
        const char* label    = (index >= 0 && index < 3) ? labels[index] : "?";
        tabSelected->set(std::format("Selected tab: {}", label));
        log(std::format("TabBar -> {}", label));
    };

    auto form = ya::ui::column("WidgetsForm")
                    .setPadding({16.0f, 12.0f})
                    .setSpacing(10.0f)
                    .child(header("WidgetsTitle", "Widgets — buttons, checkbox, slider, combo box, image, text input"))
                    .child(std::move(counter), ya::ui::boxSlot().preferredSize({180.0f, 26.0f}))
                    .child(std::move(checkA))
                    .child(ya::ui::checkBox("CheckB")
                            .setChecked(state.bCheckB)
                            .child(body("CheckB_Body", "Enable shadows"))
                            .setOnChanged([&state, log](bool bChecked)
                                          {
                                        state.bCheckB = bChecked;
                                        log(std::format("CheckBox '{}' -> {}", "CheckB", bChecked ? "on" : "off")); }))
                    .child(ya::ui::checkBox("CheckC")
                            .setChecked(state.bCheckC)
                            .child(body("CheckC_Body", "VSync"))
                            .setOnChanged([&state, log](bool bChecked)
                                          {
                                        state.bCheckC = bChecked;
                                        log(std::format("CheckBox '{}' -> {}", "CheckC", bChecked ? "on" : "off")); }))
                    .child(ya::ui::row("BrightnessRow")
                            .setSpacing(8.0f)
                            .child(body("BrightnessLabel", "Brightness"))
                            .child(std::move(slider), ya::ui::boxSlot().preferredSize({260.0f, 22.0f})))
                    .child(ya::ui::row("ApiRow")
                            .setSpacing(8.0f)
                            .child(body("ApiLabel", "Render API"))
                            .child(std::move(combo), ya::ui::boxSlot().preferredSize({180.0f, 26.0f})))
                    .child(ya::ui::row("TextureRow")
                            .setSpacing(8.0f)
                            .child(body("TextureLabel", "Texture"))
                            .child(ya::ui::image("DemoImage").setAssetPath("builtin/checkerboard"),
                                   ya::ui::boxSlot().preferredSize({96.0f, 64.0f})))
                    .child(ya::ui::row("NotesRow")
                            .setSpacing(8.0f)
                            .child(body("NotesLabel", "Notes"))
                            .child(ya::ui::textField("NotesField")
                                       .setFontSize(13)
                                       .setText(state.textFieldValue)
                                       .setOnCommit([&state, log](const std::string& text)
                                                    {
                                                    state.textFieldValue = text;
                                                    log(std::format("TextField committed: '{}'", text)); }),
                                   ya::ui::boxSlot().preferredSize({220.0f, 26.0f})))
                    .child(header("SelectableTitle", "SelectableRow — presenter-owned selection"))
                    .child(makeRow("WidgetRowAlpha", "alpha", "Alpha"),
                           ya::ui::boxSlot().preferredSize({0.0f, 24.0f}))
                    .child(makeRow("WidgetRowBeta", "beta", "Beta"), ya::ui::boxSlot().preferredSize({0.0f, 24.0f}))
                    .child(makeRow("WidgetRowGamma", "gamma", "Gamma"), ya::ui::boxSlot().preferredSize({0.0f, 24.0f}))
                    .child(header("TabBarTitle", "TabBar — horizontal strip (not the gallery rail)"))
                    .child(tabBar, ya::ui::boxSlot().preferredSize({0.0f, 28.0f}))
                    .child(ya::ui::text("WidgetTabLabel").bindText(tabSelected).setFontSize(13));
    auto page = ya::ui::border("WidgetsDemo").setStyleKey(std::string(ya::StyleKey::Panel)).child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
}

void buildInputsDemo(ya::WidgetTree& tree, ya::UICanvasPanel& parent, FDemoState& state,
                     const std::function<void(const std::string&)>& log)
{
    auto header = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::TextMuted));
    };
    auto body = [](std::string key, const std::string& text)
    {
        return ya::ui::text(std::move(key)).setText(text).setFontSize(13).setStyleKey(std::string(ya::StyleKey::Text));
    };

    auto form = ya::ui::column("InputsForm").setPadding({16.0f, 12.0f}).setSpacing(12.0f);
    form.child(header("InputsTitle", "Input controls — drag, spin, radio, color, search combo"));

    auto dragFloat            = std::make_shared<ya::UIDragFloat>("GalleryDragFloat");
    dragFloat->_value         = 3.5f;
    dragFloat->_onValueChanged = [log](float v) { log(std::format("DragFloat -> {:.2f}", v)); };
    form.child(ya::ui::row("GalleryDragRow").setSpacing(8.0f)
                  .child(body("GalleryDragFloat_Body", "DragFloat"))
                  .child(dragFloat, ya::ui::boxSlot().preferredSize({120.0f, 24.0f})));

    auto spinBox            = std::make_shared<ya::UISpinBox>("GallerySpinBox");
    spinBox->_value         = 8.0f;
    spinBox->_step          = 1.0f;
    spinBox->_onValueChanged = [log](float v) { log(std::format("SpinBox -> {:.2f}", v)); };
    form.child(ya::ui::row("GallerySpinRow").setSpacing(8.0f)
                  .child(body("GallerySpinBox_Body", "SpinBox"))
                  .child(spinBox, ya::ui::boxSlot().preferredSize({120.0f, 24.0f})));

    auto radioRow = ya::ui::row("GalleryRadioRow").setSpacing(8.0f).child(body("GalleryRadio_Body", "Radio"));
    auto radios   = std::make_shared<std::vector<std::shared_ptr<ya::UIRadioButton>>>();
    const std::vector<std::string> radioLabels = {"Shadow", "CSM", "None"};
    for (size_t i = 0; i < radioLabels.size(); ++i) {
        auto radio       = std::make_shared<ya::UIRadioButton>(std::format("GalleryRadio{}", i));
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
        radioRow.child(radio, ya::ui::boxSlot().preferredSize({90.0f, 22.0f}));
    }
    form.child(std::move(radioRow));

    auto colorEdit            = std::make_shared<ya::UIColorEdit>("GalleryColorEdit");
    colorEdit->_color         = {0.24f, 0.46f, 0.82f, 1.0f};
    colorEdit->_onColorChanged = [log](const glm::vec4& c)
    {
        log(std::format("Color -> ({:.2f}, {:.2f}, {:.2f}, {:.2f})", c.r, c.g, c.b, c.a));
    };
    form.child(ya::ui::row("GalleryColorRow").setSpacing(8.0f)
                  .child(body("GalleryColorEdit_Body", "ColorEdit"))
                  .child(colorEdit, ya::ui::boxSlot().preferredSize({280.0f, 28.0f})));

    auto searchCombo                 = std::make_shared<ya::UISearchComboBox>("GallerySearchCombo");
    searchCombo->_items              = {"Cube", "Sphere", "Capsule", "Plane", "Cone", "Torus"};
    searchCombo->_selectedIndex      = 0;
    searchCombo->_onSelectionChanged = [log](int index) { log(std::format("SearchCombo -> {}", index)); };
    form.child(ya::ui::row("GallerySearchRow").setSpacing(8.0f)
                  .child(body("GallerySearchCombo_Body", "SearchCombo"))
                  .child(searchCombo, ya::ui::boxSlot().preferredSize({180.0f, 24.0f})));

    auto page = ya::ui::border("InputsDemo")
                    .setStyleKey(std::string(ya::StyleKey::Panel))
                    .child(std::move(form), ya::ui::contentSlot().fill());
    (void)ya::ui::attach(tree, parent, std::move(page).release(), ya::ui::canvasSlot().fill());
    (void)state;
}

} // namespace guiworkbench
