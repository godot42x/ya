#include "GUI/Widgets/Controls/DragDrop.h"

#include "GUI/Widgets/UIFrameSnapshot.h"
#include "Render/Resources/FontManager.h"

namespace ya
{

UIDragDropTile::UIDragDropTile(std::string name, EKind kind)
    : UIElement(std::move(name), kind == EKind::Target ? "drag.target" : "drag.source")
    , _kind(kind)
{
    _hitFilter = EWidgetHitFilter::Stop;
}

void UIDragDropTile::paintSelf(UIFrameBuilder& builder)
{
    const FDragDropStyle& style = resolvedStyle();
    const bool           bActive = (_kind == EKind::Target) ? static_cast<bool>(_bHighlighted)
                                                           : static_cast<bool>(_bPressed);
    builder.addBrush(_layoutRect, bActive ? style.activeFill : style.normalFill);

    auto font = builder.getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    if (!font) {
        return;
    }
    const std::string& text = (_kind == EKind::Target && _bHighlighted) ? _highlightLabel : _label;
    builder.addText(_layoutRect, text, style.textColor, font, EWidgetAlignH::Center, EWidgetAlignV::Center);
}

glm::vec2 UIDragDropTile::computeDesiredSize() const
{
    const FDragDropStyle& style = resolvedStyle();
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, style.fontSize);
    const float textW = font ? font->measureText(_label) : 80.0f;
    return {textW + 20.0f, 30.0f};
}

void UIDragDropTile::appendRuntimeDiagnostics(nlohmann::json& node, const WidgetTree&) const
{
    node["control"] = {
        {"type", _kind == EKind::Target ? "dropTarget" : "dragSource"},
        {"label", _label},
        {"highlightLabel", _highlightLabel},
        {"pressed", _bPressed.get()},
        {"highlighted", _bHighlighted.get()},
    };
}

void UIDragDropTile::clearTransientInputState()
{
    _bPressed     = false;
    _bHighlighted = false;
}

} // namespace ya
