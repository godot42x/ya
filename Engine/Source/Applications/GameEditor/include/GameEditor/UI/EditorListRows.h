#pragma once

#include "Core/Log.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/SelectableRow.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/KeyedChildReconciler.h"

#include <functional>
#include <string>

namespace ya
{

inline constexpr float kEditorListRowHeight = 22.0f;
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
        .setContentPadding(FMargin{10.0f, 0.0f, 0.0f, 0.0f})
        .setOnSelect(std::move(onSelect))
        .setOnActivate(std::move(onActivate))
        .child(ui::text(key + "_Label")
                   .setText(label)
                   .setFontSize(13)
                   .setVAlign(EWidgetAlignV::Center));
}

inline void updateContentRow(UIElement& child,
                             const std::string& label,
                             const std::string& itemId,
                             bool selected,
                             std::function<void(const std::string&)> onSelect,
                             std::function<void(const std::string&)> onActivate)
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
    if (!row->getChildren().empty()) {
        if (auto* text = dynamic_cast<UIText*>(row->getChildren().front().get())) {
            text->setText(label);
        }
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

} // namespace ya
