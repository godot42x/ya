#include "GUI/Widgets/Style.h"

#include "Core/Math/GLM.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include <optional>

// ============================================================================
// Authored TStyle persistence: FBrush + every F*Style is a reflected value
// type so UIDocument can round-trip an authored TStyle object. Widgets persist
// the slot via UIElement's virtual serializeAuthoredStyle (mixin MI offset).
// Empty authored serializes as omitted/null; an object reconstitutes TStyle.
// ============================================================================

YA_REFLECT_ENUM_BEGIN(ya::FBrush::EDrawType)
YA_REFLECT_ENUM_VALUE(Image)
YA_REFLECT_ENUM_VALUE(NinePatch)
YA_REFLECT_ENUM_VALUE(Border)
YA_REFLECT_ENUM_END()

YA_REFLECT_BEGIN_EXTERNAL(ya::FBrush)
YA_REFLECT_FIELD(drawType)
YA_REFLECT_FIELD(tintColor)
YA_REFLECT_FIELD(resource)
YA_REFLECT_FIELD(margin)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTextStyle)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_FIELD(fillColor)
YA_REFLECT_FIELD(padding)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FPanelStyle)
YA_REFLECT_FIELD(fillColor)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FButtonStyle)
YA_REFLECT_FIELD(normalFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(pressedFill)
YA_REFLECT_FIELD(focusedFill)
YA_REFLECT_FIELD(disabledFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(padding)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FMenuBarItemStyle)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(normalFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(separatorColor)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTabStyle)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(normalFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(selectedFill)
YA_REFLECT_FIELD(accentColor)
YA_REFLECT_FIELD(padding)
YA_REFLECT_FIELD(separatorColor)
YA_REFLECT_FIELD(placeholderTextColor)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FSplitPaneStyle)
YA_REFLECT_FIELD(dividerFill)
YA_REFLECT_FIELD(dividerHoveredFill)
YA_REFLECT_FIELD(dividerDraggingFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FScrollBarStyle)
YA_REFLECT_FIELD(trackColor)
YA_REFLECT_FIELD(thumbColor)
YA_REFLECT_FIELD(width)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FDockSpaceStyle)
YA_REFLECT_FIELD(canvasColor)
YA_REFLECT_FIELD(dropPreviewColor)
YA_REFLECT_FIELD(dropPreviewMergeColor)
YA_REFLECT_FIELD(dropPreviewOutlineColor)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FFloatingWindowStyle)
YA_REFLECT_FIELD(bodyFill)
YA_REFLECT_FIELD(innerFill)
YA_REFLECT_FIELD(borderColor)
YA_REFLECT_FIELD(edgeAffordance)
YA_REFLECT_FIELD(titleTextColor)
YA_REFLECT_FIELD(minSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTreeViewStyle)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(selectedFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(arrowColor)
YA_REFLECT_FIELD(arrowHoveredFill)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTextFieldStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(caretColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FMenuStyle)
YA_REFLECT_FIELD(itemNormalFill)
YA_REFLECT_FIELD(itemHoveredFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(iconColor)
YA_REFLECT_FIELD(checkmarkColor)
YA_REFLECT_FIELD(shortcutColor)
YA_REFLECT_FIELD(disabledTextColor)
YA_REFLECT_FIELD(disabledIconColor)
YA_REFLECT_FIELD(separatorColor)
YA_REFLECT_FIELD(submenuArrowColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FSelectableRowStyle)
YA_REFLECT_FIELD(normalFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(selectedFill)
YA_REFLECT_FIELD(selectedHoveredFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FDragFloatStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(draggingFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(borderColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FCheckBoxStyle)
YA_REFLECT_FIELD(boxFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(checkedFill)
YA_REFLECT_FIELD(checkColor)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FComboBoxStyle)
YA_REFLECT_FIELD(fieldFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(arrowColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FSliderStyle)
YA_REFLECT_FIELD(trackFill)
YA_REFLECT_FIELD(valueFill)
YA_REFLECT_FIELD(thumbFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTableGridStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(selectedFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(headerTextColor)
YA_REFLECT_FIELD(gridColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FSpinBoxStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(buttonFill)
YA_REFLECT_FIELD(buttonHoveredFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(borderColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FRadioButtonStyle)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(dotColor)
YA_REFLECT_FIELD(dotFillColor)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FColorEditStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(channelHighlight)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FSearchComboStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(hoveredFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(caretColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

namespace ya
{

namespace
{

template <typename TStyle>
nlohmann::json serializeOptionalStyle(const std::optional<TStyle>& value)
{
    if (!value.has_value()) {
        return nullptr;
    }
    return ReflectionSerializer::serializeByRuntimeReflection(*value);
}

template <typename TStyle>
void deserializeOptionalStyle(std::optional<TStyle>& value, const nlohmann::json& j)
{
    if (j.is_null() || !j.is_object()) {
        value.reset();
        return;
    }
    TStyle style{};
    ReflectionSerializer::deserializeByRuntimeReflection(style, j, "");
    value = std::move(style);
}

template <typename TStyle>
void registerOptionalStyleHook()
{
    ReflectionSerializer::registerCustomTypeHook<std::optional<TStyle>>(
        serializeOptionalStyle<TStyle>, deserializeOptionalStyle<TStyle>);
}

} // namespace

void ensureGuiStyleReflection()
{
    static bool registered = false;
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    if (registered) {
        return;
    }
    registered = true;
    registerOptionalStyleHook<FTextStyle>();
    registerOptionalStyleHook<FPanelStyle>();
    registerOptionalStyleHook<FButtonStyle>();
    registerOptionalStyleHook<FMenuBarItemStyle>();
    registerOptionalStyleHook<FTabStyle>();
    registerOptionalStyleHook<FSplitPaneStyle>();
    registerOptionalStyleHook<FScrollBarStyle>();
    registerOptionalStyleHook<FDockSpaceStyle>();
    registerOptionalStyleHook<FFloatingWindowStyle>();
    registerOptionalStyleHook<FTreeViewStyle>();
    registerOptionalStyleHook<FTextFieldStyle>();
    registerOptionalStyleHook<FMenuStyle>();
    registerOptionalStyleHook<FSelectableRowStyle>();
    registerOptionalStyleHook<FDragFloatStyle>();
    registerOptionalStyleHook<FCheckBoxStyle>();
    registerOptionalStyleHook<FComboBoxStyle>();
    registerOptionalStyleHook<FSliderStyle>();
    registerOptionalStyleHook<FTableGridStyle>();
    registerOptionalStyleHook<FSpinBoxStyle>();
    registerOptionalStyleHook<FRadioButtonStyle>();
    registerOptionalStyleHook<FColorEditStyle>();
    registerOptionalStyleHook<FSearchComboStyle>();
}

} // namespace ya
