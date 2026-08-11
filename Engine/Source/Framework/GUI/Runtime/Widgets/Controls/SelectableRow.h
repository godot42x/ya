#pragma once

#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Selectable list / tree row (gui-app-bootstrap Phase 2).
///
/// The row is a dumb input + presentation surface: it reports a stable item
/// ID through `_onSelect` / `_onActivate` and paints `_bSelected` as given.
/// The selection set, current selection and business-object lookup belong to
/// the ToolWorkspace / presenter, never to the row. This primitive does no
/// virtualization and owns no item list.
///
/// Input semantics (same capture contract as UIButton):
///   - pointer press selects (requests focus + pointer capture); release
///     inside completes an activation;
///   - Enter / Space on the focused row activates;
///   - detach while pressed clears all transient state.
struct UISelectableRow : public UIElement
{
    YA_REFLECT_BEGIN(UISelectableRow, UIElement)
    YA_REFLECT_FIELD(_itemId, .instanceEditable())
    YA_REFLECT_FIELD(_bSelected, .instanceEditable())
    YA_REFLECT_FIELD(_normalColor, .instanceEditable())
    YA_REFLECT_FIELD(_hoveredColor, .instanceEditable())
    YA_REFLECT_FIELD(_selectedColor, .instanceEditable())
    YA_REFLECT_FIELD(_selectedHoveredColor, .instanceEditable())
    YA_REFLECT_END()

    explicit UISelectableRow(std::string name = "Row") : UIElement(std::move(name))
    {
        _hitFilter  = EWidgetHitFilter::Stop;
        _focusPolicy = EWidgetFocusPolicy::Focusable;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UISelectableRow>; }

    /// Stable item ID reported to the workspace (selection/activation).
    std::string _itemId;
    /// Presentation state, written by the presenter from the workspace
    /// selection. The row never flips this itself.
    bool _bSelected = false;

    glm::vec4 _normalColor         = {0.16f, 0.17f, 0.20f, 0.0f}; // transparent by default
    glm::vec4 _hoveredColor        = {0.24f, 0.26f, 0.31f, 1.0f};
    glm::vec4 _selectedColor       = {0.22f, 0.42f, 0.78f, 1.0f};
    glm::vec4 _selectedHoveredColor = {0.30f, 0.50f, 0.86f, 1.0f};

    std::function<void(const std::string& itemId)> _onSelect;
    std::function<void(const std::string& itemId)> _onActivate;

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override;
    [[nodiscard]] glm::vec2 computeDesiredSize() const override;

  private:
    bool _bPressed = false;
    bool _bHovered = false;
};

} // namespace ya
