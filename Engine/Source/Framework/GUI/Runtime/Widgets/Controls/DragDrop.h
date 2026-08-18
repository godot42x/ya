#pragma once

#include "GUI/Widgets/UIElement.h"

#include <functional>
#include <string>

namespace ya
{

/// Drag source wrapper (minimal): press + move beyond a threshold starts a
/// WidgetTree drag session with a host-provided payload/label. The control
/// only owns the trigger; the tree routes the session (ghost, drop targets).
struct YA_GUI_API UIDragSource : public UIElement
{
    YA_REFLECT_BEGIN(UIDragSource, UIElement)
    YA_REFLECT_FIELD(_label, .instanceEditable())
    YA_REFLECT_END()

    explicit UIDragSource(std::string name = "DragSource") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDragSource>; }

    /// Display label; the payload carried by the drag session.
    std::string _label;
    /// Returns the payload string for a drag starting from this source.
    std::function<std::string()> _makePayload;

    uint32_t  _fontSize = 13;
    glm::vec4 _textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 _backgroundColor = {0.18f, 0.20f, 0.25f, 1.0f};
    glm::vec4 _draggingColor  = {0.26f, 0.30f, 0.38f, 1.0f};

    void paintSelf(UIFrameBuilder& builder) override;
    bool handleInputEvent(const Event& event, const WidgetEventContext& ctx) override;
    void clearTransientInputState() override { _bPressed = false; }

private:
    bool      _bPressed = false;
    glm::vec2 _pressPoint{0.0f, 0.0f};
};

/// Drop target wrapper (minimal): a predicate decides which payloads are
/// accepted, a callback fires on drop, and the accepted-hover highlight is
/// drawn as a rectangle outline through the vector primitives. Reuses the
/// WidgetTree drag session (beginDrag/canAcceptDrop/onDrop/setDropHighlight).
struct YA_GUI_API UIDropTarget : public UIElement
{
    explicit UIDropTarget(std::string name = "DropTarget") : UIElement(std::move(name))
    {
        _hitFilter = EWidgetHitFilter::Stop;
    }

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIDropTarget>; }

    /// Returns true when `payload` may be dropped on this target.
    std::function<bool(const std::string& payload)> _accept;
    /// Fired when a payload is dropped (the tree clears the session first).
    std::function<void(const std::string& payload, const glm::vec2& logicalPoint)> _onDrop;

    uint32_t  _fontSize = 13;
    glm::vec4 _textColor      = {0.90f, 0.92f, 0.95f, 1.0f};
    glm::vec4 _backgroundColor = {0.12f, 0.13f, 0.17f, 1.0f};
    glm::vec4 _highlightColor = {0.24f, 0.46f, 0.82f, 1.0f};

    bool canAcceptDrop(const std::string& payload, const glm::vec2& logicalPoint) override
    {
        return _accept ? _accept(payload) : true;
    }
    void onDrop(const std::string& payload, const glm::vec2& logicalPoint) override
    {
        _bHighlighted = false;
        if (_onDrop) {
            _onDrop(payload, logicalPoint);
        }
    }
    void setDropHighlight(bool bHighlight) override
    {
        // Highlight is a paint attribute: without marking paint-dirty the
        // incremental paint cache keeps the pre-highlight draw items.
        _bHighlighted = bHighlight;
        invalidateProperty(EUIPropertyImpact::Paint);
    }

    void paintSelf(UIFrameBuilder& builder) override;
    void clearTransientInputState() override { _bHighlighted = false; }

private:
    VisualFlag _bHighlighted{*this};
};

} // namespace ya
