#pragma once

#include "GUI/Widgets/Theme.h"
#include "GUI/Widgets/UIElement.h"

#include <cstdint>
#include <string>

namespace ya
{

/// Chrome tile for a drag source or drop target. Payload / accept / drop
/// handlers stay on UIDragSourceBehavior / UIDropTargetBehavior; this widget
/// only paints themed idle/active fills + label.
struct YA_GUI_API UIDragDropTile : public UIElement, public UIStyledWidget<UIDragDropTile, FDragDropStyle>
{
    enum class EKind : uint8_t
    {
        Source,
        Target,
    };

    YA_GUI_AUTHORED_STYLE_IO(FDragDropStyle)

    explicit UIDragDropTile(std::string name = "DragDrop", EKind kind = EKind::Source);

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDragDropTile>; }

    EKind       _kind = EKind::Source;
    std::string _label;
    std::string _highlightLabel = "DROP HERE";

    void setPressed(bool pressed) { _bPressed = pressed; }
    void setHighlighted(bool highlighted) { _bHighlighted = highlighted; }

    void paintSelf(UIFrameBuilder& builder) override;
    void appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const override;
    void clearTransientInputState() override;

  private:
    VisualFlag _bPressed{*this};
    VisualFlag _bHighlighted{*this};
};

} // namespace ya
