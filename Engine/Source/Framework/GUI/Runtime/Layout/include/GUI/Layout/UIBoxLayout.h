#pragma once

#include "Core/Api.h"
#include "Core/Reflection/Reflection.h"
#include "GUI/Layout/UILayoutBase.h"
#include "GUI/Layout/UILayoutTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>

namespace ya
{

enum class EWidgetBoxLayout : uint8_t
{
    Horizontal,
    Vertical,
};

/// Main-axis arrangement of a box container (Stack role, gui-app-bootstrap
/// Phase 2): where the packed children sit when they do not fill the content
/// extent.
enum class EWidgetMainAxisAlignment : uint8_t
{
    Start,  // children packed at the content start (default)
    Center, // children centered along the main axis
    End,    // children packed at the content end
};

enum class EUIBoxSlotSizeRule : uint8_t
{
    Auto,
    Fill,
};

enum class EUIBoxSlotCrossAlignment : uint8_t
{
    Stretch,
    Start,
    Center,
    End,
};

/// Layout data carried by one UIBoxLayout parent-child edge.
class YA_GUI_API UIBoxSlot final : public UISlot
{
public:
    UIBoxSlot(UIElement& parent, UIElement& child);

    [[nodiscard]] EUIBoxSlotSizeRule getSizeRule() const { return _sizeRule; }
    [[nodiscard]] float getWeight() const { return _weight; }
    [[nodiscard]] const FMargin& getMargin() const { return _margin; }
    [[nodiscard]] EUIBoxSlotCrossAlignment getCrossAlignment() const { return _crossAlignment; }
    [[nodiscard]] const glm::vec2& getMinSize() const { return _minSize; }
    [[nodiscard]] const glm::vec2& getMaxSize() const { return _maxSize; }
    [[nodiscard]] const glm::vec2& getPreferredSize() const { return _preferredSize; }
    [[nodiscard]] bool participatesInLayout() const { return _bParticipatesInLayout; }
    [[nodiscard]] bool reservesSpaceWhenHidden() const { return _bReserveSpaceWhenHidden; }

    void setSizeRule(EUIBoxSlotSizeRule value);
    void setWeight(float value);
    void setMargin(FMargin value);
    void setMargin(glm::vec2 value) { setMargin(FMargin::hv(value)); }
    void setCrossAlignment(EUIBoxSlotCrossAlignment value);
    void setMinSize(glm::vec2 value);
    void setMaxSize(glm::vec2 value);
    void setPreferredSize(glm::vec2 value);
    void setParticipatesInLayout(bool value);
    void setReserveSpaceWhenHidden(bool value);
    void apply(const struct FBoxSlotArgs& args);
    /// Exact slot state as args: `assign(toArgs())` is the identity. `apply`
    /// is the construct-time form and treats a zero size as "unset".
    [[nodiscard]] struct FBoxSlotArgs toArgs() const;
    void assign(const struct FBoxSlotArgs& args);
    void appendRuntimeDiagnostics(nlohmann::json& node) const override;
    void serialize(nlohmann::json& node) const override;
    void deserialize(const nlohmann::json& node) override;
    [[nodiscard]] bool isAutoSizeActive() const override;

private:
    EUIBoxSlotSizeRule        _sizeRule = EUIBoxSlotSizeRule::Auto;
    float                     _weight   = 1.0f;
    FMargin                   _margin{};
    EUIBoxSlotCrossAlignment  _crossAlignment = EUIBoxSlotCrossAlignment::Stretch;
    glm::vec2                 _minSize = {0.0f, 0.0f};
    glm::vec2                 _maxSize = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    glm::vec2                 _preferredSize = {0.0f, 0.0f};
    bool                      _bParticipatesInLayout = true;
    bool                      _bReserveSpaceWhenHidden = true;
};

/// Construct-time box slot intent. Applied after the child is attached so the
/// parent-owned UIBoxSlot already exists.
struct FBoxSlotArgs
{
    using SlotType = UIBoxSlot;

    EUIBoxSlotSizeRule       sizeRule        = EUIBoxSlotSizeRule::Auto;
    float                    weight          = 1.0f;
    FMargin                  margin          = {};
    EUIBoxSlotCrossAlignment crossAlignment  = EUIBoxSlotCrossAlignment::Stretch;
    glm::vec2                minSize         = {0.0f, 0.0f};
    glm::vec2                maxSize         = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    /// Non-zero on an axis overrides the child's desired size for that axis.
    glm::vec2                preferredSize  = {0.0f, 0.0f};
    bool                     participatesInLayout    = true;
    bool                     reserveSpaceWhenHidden  = true;
};

/// The first formal layout: horizontal/vertical box packing with layout-owned
/// container properties and per-child UIBoxSlot data.
class YA_GUI_API UIBoxLayout final : public UILayout
{
public:
    [[nodiscard]] EWidgetBoxLayout getDirection() const { return _direction; }
    [[nodiscard]] float getSpacing() const { return _spacing; }
    [[nodiscard]] const glm::vec2& getPadding() const { return _padding; }
    [[nodiscard]] EWidgetMainAxisAlignment getMainAxisAlignment() const { return _mainAxisAlignment; }
    [[nodiscard]] bool clipsChildren() const { return _bClipChildren; }
    [[nodiscard]] bool stretchesLastChild() const { return _bStretchLastChild; }

    void setDirection(EWidgetBoxLayout value);
    void setSpacing(float value);
    void setPadding(glm::vec2 value);
    void setMainAxisAlignment(EWidgetMainAxisAlignment value);
    void setClipsChildren(bool value);
    void setStretchLastChild(bool value);

    [[nodiscard]] std::unique_ptr<UISlot> createSlot(UIElement& parent, UIElement& child) const override;
    [[nodiscard]] glm::vec2 measure(const UIElement& parent) const override;
    void onArrange(UIElement& parent, const Rect2D& rect) const override;

    // Container configuration is reflected (designer inspector + UIDocument)
    // while staying layout-owned: writes go through the raw member and the
    // editing contexts aggregate layout invalidation themselves.
    YA_REFLECT_BEGIN(UIBoxLayout)
        YA_REFLECT_FIELD(_direction, .instanceEditable())
        YA_REFLECT_FIELD(_spacing, .instanceEditable())
        YA_REFLECT_FIELD(_padding, .instanceEditable())
        YA_REFLECT_FIELD(_mainAxisAlignment, .instanceEditable())
        YA_REFLECT_FIELD(_bClipChildren, .instanceEditable())
        YA_REFLECT_FIELD(_bStretchLastChild, .instanceEditable())
    YA_REFLECT_END()

private:
    EWidgetBoxLayout         _direction         = EWidgetBoxLayout::Horizontal;
    float                    _spacing           = 4.0f;
    glm::vec2                _padding           = {0.0f, 0.0f};
    EWidgetMainAxisAlignment _mainAxisAlignment = EWidgetMainAxisAlignment::Start;
    bool                     _bClipChildren     = false;
    bool                     _bStretchLastChild = false;
};

} // namespace ya
