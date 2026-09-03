#include "GUI/Widgets/Style.h"

#include "Core/Log.h"
#include "Core/Reflection/Reflection.h"

#include <string>

// ============================================================================
// Authored TStyle persistence: FBrush + every F*Style is a reflected value
// type so UIDocument can round-trip a sparse `_authoredStyle` patch (or a
// full TStyle object, which is a freeze). Widgets persist the slot
// via UIElement's virtual serializeAuthoredStyle (mixin MI offset).
// Empty authored serializes as omitted/null; an object reconstitutes the patch.
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
YA_REFLECT_FIELD(selectedFill)
YA_REFLECT_FIELD(errorFill)
YA_REFLECT_FIELD(dropTargetFill)
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
YA_REFLECT_FIELD(dropIndicator)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FTextFieldStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(errorFill)
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
YA_REFLECT_FIELD(dropTargetFill)
YA_REFLECT_FIELD(errorFill)
YA_REFLECT_FIELD(disabledFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FDragFloatStyle)
YA_REFLECT_FIELD(backgroundFill)
YA_REFLECT_FIELD(draggingFill)
YA_REFLECT_FIELD(errorFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(borderColor)
YA_REFLECT_FIELD(errorBorderColor)
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

YA_REFLECT_BEGIN_EXTERNAL(ya::FImageStyle)
YA_REFLECT_FIELD(placeholderFill)
YA_REFLECT_FIELD(errorFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FPopupStyle)
YA_REFLECT_FIELD(modalFill)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::FDragDropStyle)
YA_REFLECT_FIELD(normalFill)
YA_REFLECT_FIELD(activeFill)
YA_REFLECT_FIELD(textColor)
YA_REFLECT_FIELD(fontSize)
YA_REFLECT_END_EXTERNAL()

namespace ya
{

namespace
{

[[nodiscard]] bool isLayoutAffectingStyleField(std::string_view field)
{
    // Name-based, not a per-type table. Do not treat generic "width" as
    // layout: FScrollBarStyle.width is overlay paint thickness.
    return field == "fontSize" || field == "padding" || field == "minSize";
}

} // namespace

FStyleFieldImpact lookupStyleFieldImpact(type_index_t styleType, std::string_view field)
{
    FStyleFieldImpact impact;
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    Class* cls = ClassRegistry::instance().getClass(styleType);
    if (!cls) {
        return impact;
    }
    Property* prop = cls->getProperty(std::string(field));
    if (!prop) {
        return impact;
    }
    if (prop->typeIndex == ya::type_index_v<FBrush>) {
        impact.bResource = true;
    }
    if (isLayoutAffectingStyleField(field)) {
        impact.bLayout = true;
    }
    return impact;
}

FStyleFieldImpact lookupStylePatchImpact(type_index_t styleType, const nlohmann::json& patch)
{
    FStyleFieldImpact combined;
    if (patch.is_null() || !patch.is_object() || patch.empty()) {
        return combined;
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.key() == "__base__") {
            continue;
        }
        const FStyleFieldImpact field = lookupStyleFieldImpact(styleType, it.key());
        combined.bLayout   = combined.bLayout || field.bLayout;
        combined.bResource = combined.bResource || field.bResource;
    }
    return combined;
}

namespace
{

struct FStyleCatalogEntry
{
    std::string_view key;
    type_index_t     styleType;
};

const FStyleCatalogEntry kStyleCatalog[] = {
#define YA_GUI_STYLE_KEY_ENTRY(Type, Name, Str) {Str, ya::type_index_v<Type>},
    YA_GUI_STYLE_CATALOG(YA_GUI_STYLE_KEY_ENTRY)
#undef YA_GUI_STYLE_KEY_ENTRY
};

constexpr std::string_view kEditorPrefix = "editor.";

StyleCatalogDiagnostics s_styleCatalogDiagnostics;

[[nodiscard]] std::string_view catalogKeyForLookup(std::string_view key)
{
    if (key.starts_with(kEditorPrefix)) {
        return key.substr(kEditorPrefix.size());
    }
    return key;
}

} // namespace

EStyleKeyLookup lookupStyleKey(std::string_view key, type_index_t styleType)
{
    if (key.empty()) {
        return EStyleKeyLookup::Empty;
    }
    const std::string_view catalogKey = catalogKeyForLookup(key);
    if (catalogKey.empty()) {
        return EStyleKeyLookup::UnknownKey;
    }

    bool bKeyKnown = false;
    for (const FStyleCatalogEntry& entry : kStyleCatalog) {
        if (entry.key != catalogKey) {
            continue;
        }
        bKeyKnown = true;
        if (entry.styleType == styleType) {
            return EStyleKeyLookup::Known;
        }
    }
    return bKeyKnown ? EStyleKeyLookup::TypeMismatch : EStyleKeyLookup::UnknownKey;
}

StyleCatalogDiagnostics getStyleCatalogDiagnostics()
{
    return s_styleCatalogDiagnostics;
}

void diagnoseStyleKey(std::string_view key, type_index_t styleType)
{
    switch (lookupStyleKey(key, styleType)) {
    case EStyleKeyLookup::Known:
    case EStyleKeyLookup::Empty:
        break;
    case EStyleKeyLookup::UnknownKey:
        ++s_styleCatalogDiagnostics.unknownKeys;
        YA_CORE_WARN("style catalog: unknown key '{}'", key);
        break;
    case EStyleKeyLookup::TypeMismatch:
        ++s_styleCatalogDiagnostics.typeMismatches;
        YA_CORE_WARN("style catalog: key '{}' does not match this style type", key);
        break;
    }
}

const FBrush& resolveVisualFill(const FVisualChrome& chrome, EWidgetVisualFlags flags)
{
    // Exclusive precedence is the product contract: disabled chrome must not
    // flash hover, drop overlay wins over selection, validation error wins
    // over press, press wins over hover, selected+hovered uses the
    // combination brush, focus is the last non-normal fill.
    if (hasVisualFlag(flags, EWidgetVisualFlag::Disabled)) {
        return chrome.disabled;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::DropTarget)) {
        return chrome.dropTarget;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Error)) {
        return chrome.error;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Pressed)) {
        return chrome.pressed;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Selected) &&
        hasVisualFlag(flags, EWidgetVisualFlag::Hovered)) {
        return chrome.selectedHovered;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Selected)) {
        return chrome.selected;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Hovered)) {
        return chrome.hovered;
    }
    if (hasVisualFlag(flags, EWidgetVisualFlag::Focused)) {
        return chrome.focused;
    }
    return chrome.normal;
}

FVisualChrome visualChrome(const FButtonStyle& style)
{
    return FVisualChrome{
        .normal          = style.normalFill,
        .hovered         = style.hoveredFill,
        .pressed         = style.pressedFill,
        .focused         = style.focusedFill,
        .disabled        = style.disabledFill,
        .selected        = style.selectedFill,
        .selectedHovered = style.selectedFill,
        .error           = style.errorFill,
        .dropTarget      = style.dropTargetFill,
    };
}

FVisualChrome visualChrome(const FSelectableRowStyle& style)
{
    return FVisualChrome{
        .normal          = style.normalFill,
        .hovered         = style.hoveredFill,
        .pressed         = style.hoveredFill,
        .focused         = style.selectedFill,
        .disabled        = style.disabledFill,
        .selected        = style.selectedFill,
        .selectedHovered = style.selectedHoveredFill,
        .error           = style.errorFill,
        .dropTarget      = style.dropTargetFill,
    };
}

namespace
{

FVisualChrome chromeHoverPair(const FBrush& normal, const FBrush& hovered)
{
    return FVisualChrome{
        .normal          = normal,
        .hovered         = hovered,
        .pressed         = hovered,
        .focused         = hovered,
        .disabled        = normal,
        .selected        = hovered,
        .selectedHovered = hovered,
        .error           = hovered,
        .dropTarget      = hovered,
    };
}

} // namespace

FVisualChrome visualChrome(const FCheckBoxStyle& style)
{
    return FVisualChrome{
        .normal          = style.boxFill,
        .hovered         = style.hoveredFill,
        .pressed         = style.hoveredFill,
        .focused         = style.checkedFill,
        .disabled        = style.boxFill,
        .selected        = style.checkedFill,
        .selectedHovered = style.checkedFill,
        .error           = style.checkedFill,
        .dropTarget      = style.checkedFill,
    };
}

FVisualChrome visualChrome(const FMenuBarItemStyle& style)
{
    return chromeHoverPair(style.normalFill, style.hoveredFill);
}

FVisualChrome visualChrome(const FTabStyle& style)
{
    return FVisualChrome{
        .normal          = style.normalFill,
        .hovered         = style.hoveredFill,
        .pressed         = style.hoveredFill,
        .focused         = style.selectedFill,
        .disabled        = style.normalFill,
        .selected        = style.selectedFill,
        .selectedHovered = style.selectedFill,
        .error           = style.selectedFill,
        .dropTarget      = style.selectedFill,
    };
}

FVisualChrome visualChrome(const FComboBoxStyle& style)
{
    return chromeHoverPair(style.fieldFill, style.hoveredFill);
}

FVisualChrome visualChrome(const FMenuStyle& style)
{
    return chromeHoverPair(style.itemNormalFill, style.itemHoveredFill);
}

FVisualChrome visualChrome(const FTableGridStyle& style)
{
    const FBrush idle = FBrush::solid({0.0f, 0.0f, 0.0f, 0.0f});
    return FVisualChrome{
        .normal          = idle,
        .hovered         = style.hoveredFill,
        .pressed         = style.hoveredFill,
        .focused         = style.selectedFill,
        .disabled        = idle,
        .selected        = style.selectedFill,
        .selectedHovered = style.selectedFill,
        .error           = style.selectedFill,
        .dropTarget      = style.selectedFill,
    };
}

FVisualChrome visualChrome(const FTreeViewStyle& style)
{
    const FBrush idle = FBrush::solid({0.0f, 0.0f, 0.0f, 0.0f});
    return FVisualChrome{
        .normal          = idle,
        .hovered         = style.hoveredFill,
        .pressed         = style.hoveredFill,
        .focused         = style.selectedFill,
        .disabled        = idle,
        .selected        = style.selectedFill,
        .selectedHovered = style.selectedFill,
        .error           = style.selectedFill,
        .dropTarget      = style.selectedFill,
    };
}

} // namespace ya
