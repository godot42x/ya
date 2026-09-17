#pragma once

#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Widgets/UIElement.h"

namespace ya
{

/// Pure canvas-layout host. Children are placed by parent-owned canvas slots
/// (anchors / insets / size modes). This widget does not paint; a filled
/// surface is `UIBorder`.
struct YA_GUI_API UICanvasPanel : public UIElement
{
    using SlotArgs = FCanvasSlotArgs;

    YA_REFLECT_BEGIN(UICanvasPanel, UIElement)
    YA_REFLECT_END()

    explicit UICanvasPanel(std::string name = "CanvasPanel")
        : UIElement(std::move(name))
    {
        installLayout(std::make_unique<UICanvasLayout>());
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICanvasPanel>; }

    [[nodiscard]] UICanvasLayout* getCanvasLayout() const
    {
        return dynamic_cast<UICanvasLayout*>(getLayout());
    }
    [[nodiscard]] UICanvasSlot* getCanvasSlot(const UIElement& child) const
    {
        if (UISlot* edge = getSlotForChild(child)) {
            return edge->as<UICanvasSlot>();
        }
        return nullptr;
    }

    void appendRuntimeLayoutDiagnostics(nlohmann::json& node) const override { node["type"] = "canvas"; }
};

} // namespace ya
