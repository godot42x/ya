#pragma once

#include "Core/Log.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/KeyedChildReconciler.h"
#include "GameEditor/UI/EditorTheme.h"

#include <functional>
#include <string>

namespace ya
{

inline constexpr float kEditorListRowHeight = editor_density::kListRowHeight;
inline constexpr float kEditorListRowSpacing = 2.0f;
inline constexpr size_t kEditorListOverscan = 2;

inline ui::UISelectableRowWidgetBuilder contentRow(const std::string& key,
                                                   const std::string& label,
                                                   const std::string& itemId,
                                                   std::function<void(const std::string&)> onSelect,
                                                   std::function<void(const std::string&)> onActivate)
{
    return ui::selectableRow(key)
        .setItemId(itemId)
        .setContentPadding(FMargin{6.0f, 0.0f, 0.0f, 0.0f})
        .setOnSelect(std::move(onSelect))
        .setOnActivate(std::move(onActivate))
        .child(ui::row(key + "_Content")
                   .setSpacing(editor_density::kControlSpacing)
                   .child(ui::image(key + "_Icon").setAssetPath(editor_icons::kFile),
                          ui::boxSlot().preferredSize({editor_density::kListIconSize,
                                                       editor_density::kListIconSize}))
                   .child(ui::text(key + "_Label")
                              .setText(label)
                              .setFontSize(13)
                              .setVAlign(EWidgetAlignV::Center)));
}

inline UIImage* contentRowIcon(UIElement& row)
{
    if (row.getChildren().empty()) {
        return nullptr;
    }
    UIElement* content = row.getChildren().front().get();
    for (const auto& child : content->getChildren()) {
        if (auto* image = dynamic_cast<UIImage*>(child.get())) {
            return image;
        }
    }
    return nullptr;
}

inline UIText* contentRowLabel(UIElement& row)
{
    if (row.getChildren().empty()) {
        return nullptr;
    }
    UIElement* content = row.getChildren().front().get();
    for (const auto& child : content->getChildren()) {
        if (auto* text = dynamic_cast<UIText*>(child.get())) {
            return text;
        }
    }
    return nullptr;
}

inline void updateContentRow(UIElement& child,
                             const std::string& label,
                             const std::string& itemId,
                             bool selected,
                             std::function<void(const std::string&)> onSelect,
                             std::function<void(const std::string&)> onActivate,
                             bool bDirectory = false)
{
    auto* row = dynamic_cast<UISelectableRow*>(&child);
    if (!row) {
        YA_CORE_ERROR("EditorListRows: keyed content row '{}' is not a UISelectableRow", child._name);
        return;
    }
    row->_itemId = itemId;
    row->setSelected(selected);
    row->_onSelect = std::move(onSelect);
    row->_onActivate = std::move(onActivate);
    if (UIText* text = contentRowLabel(*row)) {
        text->setText(label);
    }
    if (UIImage* icon = contentRowIcon(*row)) {
        icon->_assetPath = bDirectory ? editor_icons::kFolder : editor_icons::kFile;
        icon->markPaintDirty();
    }
}

inline UIKeyedChildReconciler::Factory makeContentRowFactory()
{
    return [](const std::string& key, size_t) {
        return contentRow(key, key, key, {}, {}).release();
    };
}

inline void bindEditorListRowSlot(UISlot& slot, const std::string&, size_t)
{
    if (auto* box = slot.as<UIBoxSlot>()) {
        box->setPreferredSize({0.0f, kEditorListRowHeight});
    }
}

inline ui::UIButtonWidgetBuilder labeledButton(std::string key, const std::string& label)
{
    std::string labelKey = key + "_Label";
    return ui::button(std::move(key))
        .child(ui::text(std::move(labelKey))
                   .setText(label)
                   .setFontSize(13)
                   .setHAlign(EWidgetAlignH::Center)
                   .setVAlign(EWidgetAlignV::Center));
}

inline ui::UIButtonWidgetBuilder iconLabeledButton(std::string key,
                                                   const std::string& label,
                                                   const char* assetPath)
{
    const std::string contentKey = key + "_Content";
    const std::string iconKey    = key + "_Icon";
    const std::string labelKey   = key + "_Label";
    return ui::button(std::move(key))
        .child(ui::row(contentKey)
                   .setSpacing(4.0f)
                   .child(ui::image(iconKey).setAssetPath(assetPath),
                          ui::boxSlot().preferredSize({editor_density::kToolbarIconSize,
                                                       editor_density::kToolbarIconSize}))
                   .child(ui::text(labelKey)
                              .setText(label)
                              .setFontSize(13)
                              .setHAlign(EWidgetAlignH::Center)
                              .setVAlign(EWidgetAlignV::Center)));
}

} // namespace ya
