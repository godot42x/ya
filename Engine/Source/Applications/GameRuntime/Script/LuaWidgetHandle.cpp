#include "LuaWidgetHandle.h"

#include "WidgetScriptRefs.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "Core/Delegate.h"
#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Widgets/Controls/Menu.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Script/Lua/ScriptInstanceOps.h"

#include <array>
#include <string_view>
#include <unordered_map>

namespace ya
{
namespace
{

UIElement* findInSubtree(UIElement& node, std::string_view name)
{
    if (node._name == name) {
        return &node;
    }
    for (const UIElementRef& child : node.getChildren()) {
        if (UIElement* found = child ? findInSubtree(*child, name) : nullptr) {
            return found;
        }
    }
    return nullptr;
}

UIElement* requireLive(void* self, const script::ScriptRef& ref, const char* operation)
{
    auto* widget = static_cast<UIElement*>(self);
    if (widget && widgetLive(widget)) {
        return widget;
    }
    warnWidgetGone(ref.a != 0 ? ref.a : (widget ? widget->getRuntimeId() : 0), operation);
    return nullptr;
}

UIElement* requireLive(UIElement& widget, const char* operation)
{
    if (widgetLive(&widget)) {
        return &widget;
    }
    warnWidgetGone(widget.getRuntimeId(), operation);
    return nullptr;
}

script::ScriptValue widgetValue(GameUIHost& host, const UIElementRef& widget)
{
    const script::ScriptRef ref = widgetScriptRef(host, widget);
    if (!ref) {
        return {};
    }
    return script::scriptMake(ref);
}

script::ScriptNativeField nameField()
{
    return script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            UIElement* widget = requireLive(self, ref, "name");
            if (!widget) {
                return {};
            }
            return script::scriptMake(widget->_name);
        },
    };
}

script::ScriptNativeField visibleField()
{
    return script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            UIElement* widget = requireLive(self, ref, "visible");
            if (!widget) {
                return {};
            }
            return script::scriptMake(widget->isVisibleForRender());
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            UIElement* widget = requireLive(self, ref, "visible");
            if (!widget) {
                return;
            }
            widget->setVisibility(script::scriptToBool(value) ? EWidgetVisibility::Visible : EWidgetVisibility::Hidden);
        },
    };
}

UIButton* liveButton(uint64_t runtimeId)
{
    auto* button = dynamic_cast<UIButton*>(resolveWidget(runtimeId));
    if (!button || !widgetLive(button)) {
        warnWidgetGone(runtimeId, "onClicked");
        return nullptr;
    }
    return button;
}

script::ScriptValue addClicked(WidgetScriptButtonClicked& event, script::ScriptArgs args)
{
    UIButton* button = liveButton(event.widgetRuntimeId);
    if (!button) {
        return script::scriptMake(int64_t{0});
    }
    const auto* owner = args.size() > 0 ? script::scriptIf<script::ScriptSelfHandle>(args[0]) : nullptr;
    const auto* fn    = args.size() > 1 ? script::scriptIf<script::ScriptFnHandle>(args[1]) : nullptr;
    if (!owner || owner->runtimeId == 0 || !fn || fn->id == 0) {
        if (!fn || fn->id == 0) {
            YA_CORE_ERROR("Lua Button.onClicked:add: callback is not a function");
        }
        else {
            YA_CORE_ERROR("Lua Button.onClicked:add: the owner must be a live script's self");
        }
        return script::scriptMake(int64_t{0});
    }
    const std::shared_ptr<void> kept = scriptInstanceListenerOwner(owner->runtimeId);
    if (!kept || !retainInstanceFunction(owner->runtimeId, *fn)) {
        YA_CORE_ERROR("Lua Button.onClicked:add: the owner must be a live script's self");
        return script::scriptMake(int64_t{0});
    }
    const script::ScriptFnHandle   held = *fn;
    const script::ScriptSelfHandle self = *owner;
    const std::weak_ptr<void>      weak = kept;
    const DelegateHandle           handle = button->onClicked.addWeakLambda(weak, [held, self, weak]() {
        if (weak.expired() || !scriptFunctionAlive(held)) {
            return;
        }
        if (script::IScriptCallbacks* callbacks = script::scriptCallbacks()) {
            callbacks->invoke(held, self, {});
        }
    });
    return script::scriptMake(static_cast<int64_t>(handle));
}

script::ScriptValue removeClicked(WidgetScriptButtonClicked& event, script::ScriptArgs args)
{
    UIButton* button = liveButton(event.widgetRuntimeId);
    if (!button || args.empty()) {
        return script::scriptMake(false);
    }
    const int64_t handle = script::scriptToInteger(args[0]);
    if (handle <= 0) {
        return script::scriptMake(false);
    }
    return script::scriptMake(button->onClicked.remove(static_cast<DelegateHandle>(handle)));
}

script::ScriptValue removeAllClicked(WidgetScriptButtonClicked& event, script::ScriptArgs args)
{
    UIButton* button = liveButton(event.widgetRuntimeId);
    const auto* owner = !args.empty() ? script::scriptIf<script::ScriptSelfHandle>(args[0]) : nullptr;
    if (!button || !owner || owner->runtimeId == 0) {
        return script::scriptMake(int64_t{0});
    }
    const std::shared_ptr<void> kept = scriptInstanceListenerOwner(owner->runtimeId);
    if (!kept) {
        return script::scriptMake(int64_t{0});
    }
    return script::scriptMake(static_cast<int64_t>(button->onClicked.removeAll(kept.get())));
}

template <typename T>
T* liveAs(void* self, const script::ScriptRef& ref, const char* operation)
{
    return dynamic_cast<T*>(requireLive(self, ref, operation));
}

// ── UIMenu script surface ─────────────────────────────────────────────────
// The anchored action menu: the game appends rows (one call each — a Lua
// table array has no script crossing), positions the popup at a presentation
// pixel (the acting unit's head), and receives the chosen row id through one
// retained callback, exactly like Button.onClicked.
//
//   menu:clearItems()
//   menu:addItem("attack", "Attack")
//   menu:onChosen:add(self, function(id) ... end)
//   menu:openAtPixel(x, y)   -- presentation pixels; the host maps to tree coords
//   menu:close()

UIMenu* liveMenu(void* self, const script::ScriptRef& ref, const char* operation)
{
    return liveAs<UIMenu>(self, ref, operation);
}

/// Per-menu script rows: the id/label pairs the game appended, plus the
/// script callback that receives a chosen id. Keyed by the menu's runtime id;
/// the callback dies with its script instance (listener owner held weakly).
struct MenuScriptRows
{
    struct Row
    {
        std::string id;
        std::string label;
    };
    std::vector<Row>                       rows;
    script::ScriptFnHandle                 onChosen;
    std::weak_ptr<void>                    owner;
};
std::unordered_map<uint64_t, MenuScriptRows>& menuScriptRows()
{
    static std::unordered_map<uint64_t, MenuScriptRows> table;
    return table;
}

void invokeMenuChosen(UIMenu& menu, size_t index)
{
    auto        it  = menuScriptRows().find(menu.getRuntimeId());
    if (it == menuScriptRows().end()) {
        return;
    }
    MenuScriptRows& stored = it->second;
    if (stored.onChosen.id == 0 || !scriptFunctionAlive(stored.onChosen)) {
        return;
    }
    const std::string id = index < stored.rows.size() ? stored.rows[index].id : std::to_string(index);
    if (script::IScriptCallbacks* callbacks = script::scriptCallbacks()) {
        // The receiver wants the id, not a self: an empty self handle (the
        // function was retained from a live instance, so its closure already
        // knows its own self).
        script::ScriptSelfHandle self;
        script::ScriptValue arg = script::scriptMake(id);
        const std::array<script::ScriptValue, 1> args{arg};
        callbacks->invoke(stored.onChosen, self, args);
    }
}

/// Rebuild the menu's items from the stored rows; every row's action reports
/// its index. Raw `&menu` capture is safe: rows live only while the menu does.
void rebuildMenuItems(UIMenu& menu, MenuScriptRows& stored)
{
    std::vector<UIMenu::FItem> items;
    items.reserve(stored.rows.size());
    for (std::size_t i = 0; i < stored.rows.size(); ++i) {
        UIMenu::FItem item;
        item.label = stored.rows[i].label;
        item.action = [&menu, i]() { invokeMenuChosen(menu, i); };
        items.push_back(std::move(item));
    }
    menu.setItems(std::move(items));
}

void registerMenuScriptMethods()
{
    UIMenu::__ensure_reflection_registered();
    const type_index_t menu = type_index_v<UIMenu>;
    script::setScriptTypeName(menu, "Menu");

    script::addMethod(menu, "clearItems", script::fn([](UIMenu& menu) {
        menuScriptRows()[menu.getRuntimeId()] = MenuScriptRows{};
        menu.setItems({});
    }));
    script::addMethod(menu, "addItem", script::fnVariadic<UIMenu&>(
        [](UIMenu& menu, script::ScriptArgs args) -> script::ScriptValue {
            if (args.size() < 2) {
                throw script::ScriptError("Menu.addItem expects (id, label)");
            }
            MenuScriptRows& stored = menuScriptRows()[menu.getRuntimeId()];
            stored.rows.push_back(MenuScriptRows::Row{
                .id = script::scriptToString(args[0]),
                .label = script::scriptToString(args[1]),
            });
            rebuildMenuItems(menu, stored);
            return {};
        }));
    script::addMethod(menu, "onChosen", script::fnVariadic<UIMenu&>(
        [](UIMenu& menu, script::ScriptArgs args) -> script::ScriptValue {
            if (args.size() < 2) {
                throw script::ScriptError("Menu.onChosen expects (self, function)");
            }
            const auto* owner = script::scriptIf<script::ScriptSelfHandle>(args[0]);
            const auto* fn    = script::scriptIf<script::ScriptFnHandle>(args[1]);
            if (!owner || owner->runtimeId == 0 || !fn || fn->id == 0) {
                throw script::ScriptError("Menu.onChosen expects (self, function)");
            }
            MenuScriptRows& stored = menuScriptRows()[menu.getRuntimeId()];
            stored.owner = scriptInstanceListenerOwner(owner->runtimeId);
            if (!retainInstanceFunction(owner->runtimeId, *fn)) {
                throw script::ScriptError("Menu.onChosen: the owner must be a live script's self");
            }
            stored.onChosen = *fn;
            return {};
        }));
    script::addMethod(menu, "openAtPixel", script::fnVariadic<UIMenu&>(
        [](UIMenu& menu, script::ScriptArgs args) -> script::ScriptValue {
            if (args.size() < 2) {
                throw script::ScriptError("Menu.openAtPixel expects (x, y)");
            }
            GameUIHost* host = widgetHost(menu.getRuntimeId());
            if (!host) {
                throw script::ScriptError("Menu.openAtPixel: the menu is not mounted");
            }
            // Presentation (view device) pixels → tree logical: the host owns
            // the one mapping (`presentationToLogical`), shared with pointer
            // events, so an anchored popup lands on the drawn pixel on any DPI.
            const glm::vec2 logical =
                host->presentationToLogical(glm::vec2(static_cast<float>(script::scriptToNumber(args[0])),
                                                      static_cast<float>(script::scriptToNumber(args[1]))));
            WidgetTree& tree = host->getTree();
            menu.openAt(tree, logical);
            return {};
        }));
    script::addMethod(menu, "setHighlight", script::fn([](UIMenu& menu, int64_t index) {
        menu.setHighlight(static_cast<int>(index));
    }));
    script::addMethod(menu, "close", script::fn([](UIMenu& menu) {
        menu.close();
    }));
}


} // namespace

script::ScriptRef widgetScriptRef(GameUIHost& host, const UIElementRef& widget)
{
    if (!widget) {
        return {};
    }
    (void)widgetRefKind();
    trackWidget(host, widget);
    const type_index_t type = widget->getTypeIndex();
    if (type != type_index_v<UIElement>) {
        script::addScriptBase(type, type_index_v<UIElement>);
    }
    script::addNativeField(type, "name", nameField());
    return script::ScriptRef{
        .type = type,
        .kind = widgetRefKind(),
        .a    = widget->getRuntimeId(),
        .b    = 0,
    };
}

void registerWidgetNativeMethods()
{
    UIElement::__ensure_reflection_registered();
    ensureWidgetScriptObjectsReflected();
    registerMenuScriptMethods();

    const type_index_t element = type_index_v<UIElement>;
    const type_index_t timer   = type_index_v<WidgetScriptTimer>;
    const type_index_t clicked = type_index_v<WidgetScriptButtonClicked>;

    script::setScriptTypeName(element, "Widget");
    script::setScriptTypeName(timer, "UITimer");
    script::setScriptTypeName(clicked, "Event");

    script::addMethod(element, "find", script::fnVariadic<UIElement&, std::string>([](UIElement& widget, script::ScriptArgs args) -> script::ScriptValue {
        UIElement* live = requireLive(widget, "find");
        GameUIHost* host = live ? widgetHost(live->getRuntimeId()) : nullptr;
        if (!live || !host || args.empty()) {
            return {};
        }
        const std::string name = script::scriptToString(args[0]);
        UIElement* found = nullptr;
        if (!live->isAttached()) {
            found = findInSubtree(*live, name);
        }
        else if (UIElement* root = host->entryRootOf(*live)) {
            found = host->findInEntry(*root, name).get();
        }
        return found ? widgetValue(*host, found->shared_from_this()) : script::ScriptValue{};
    }));
    script::addMethod(element, "destroy", script::fn([](UIElement& widget) {
        if (UIElement* live = requireLive(widget, "destroy")) {
            if (GameUIHost* host = widgetHost(live->getRuntimeId())) {
                host->queueDestroy(*live);
            }
        }
    }));
    script::addMethod(element, "layout", script::fn([](UIElement& widget) {
        if (UIElement* live = requireLive(widget, "layout")) {
            if (GameUIHost* host = widgetHost(live->getRuntimeId())) {
                host->layoutNow();
            }
        }
    }));
    // Canvas-slot geometry from a script: the positional twin of `rect()`
    // (which only reads). A HUD that anchors a bar to a unit or resizes a
    // gauge per frame drives its slot here. No-op for a widget whose slot is
    // not a canvas slot (box/table layouts own their children's geometry).
    script::addMethod(element, "setSize", script::fnVariadic<UIElement&>(
        [](UIElement& widget, script::ScriptArgs args) -> script::ScriptValue {
            if (args.size() < 2) {
                throw script::ScriptError("Widget.setSize expects (width, height)");
            }
            if (UISlot* slot = widget.getSlot()) {
                if (auto* canvas = slot->as<UICanvasSlot>()) {
                    canvas->setFixedSize({static_cast<float>(script::scriptToNumber(args[0])),
                                          static_cast<float>(script::scriptToNumber(args[1]))});
                    canvas->setWidthSizeMode(EWidgetSizeMode::Fixed);
                    canvas->setHeightSizeMode(EWidgetSizeMode::Fixed);
                }
            }
            return {};
        }));
    script::addMethod(element, "setPosition", script::fnVariadic<UIElement&>(
        [](UIElement& widget, script::ScriptArgs args) -> script::ScriptValue {
            if (args.size() < 2) {
                throw script::ScriptError("Widget.setPosition expects (x, y)");
            }
            if (UISlot* slot = widget.getSlot()) {
                if (auto* canvas = slot->as<UICanvasSlot>()) {
                    canvas->setOffset({static_cast<float>(script::scriptToNumber(args[0])),
                                       static_cast<float>(script::scriptToNumber(args[1]))});
                }
            }
            return {};
        }));
    script::addMethod(element, "rect", script::fn([](UIElement& widget) -> script::ScriptValue {
        UIElement* live = requireLive(widget, "rect");
        if (!live) {
            return {};
        }
        const Rect2D& rect = live->getLayoutRect();
        return script::scriptMake(script::ScriptRect{
            .x = rect.pos.x,
            .y = rect.pos.y,
            .w = rect.extent.x,
            .h = rect.extent.y,
        });
    }));
    script::addMethod(timer, "cancel", script::fn([](WidgetScriptTimer& timer) { cancelWidgetTimer(timer); }));
    script::addMethod(clicked, "add", script::fnVariadic<WidgetScriptButtonClicked&, script::ScriptSelfHandle, script::ScriptFnHandle>(&addClicked));
    script::addMethod(clicked, "remove", script::fnVariadic<WidgetScriptButtonClicked&, int64_t>(&removeClicked));
    script::addMethod(clicked, "removeAll", script::fnVariadic<WidgetScriptButtonClicked&, script::ScriptSelfHandle>(&removeAllClicked));
}

void registerWidgetScriptSurface()
{
    registerRuntimeScriptNatives();

    const type_index_t element = type_index_v<UIElement>;
    const type_index_t text    = type_index_v<UIText>;
    const type_index_t button  = type_index_v<UIButton>;
    const type_index_t image   = type_index_v<UIImage>;
    const type_index_t border  = type_index_v<UIBorder>;

    script::setScriptTypeName(element, "Widget");
    script::setScriptTypeName(text, "Text");
    script::setScriptTypeName(button, "Button");
    script::setScriptTypeName(image, "Image");
    script::setScriptTypeName(border, "Border");

    script::addNativeField(element, "valid", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef&) -> script::ScriptValue {
            return script::scriptMake(self && widgetLive(static_cast<const UIElement*>(self)));
        },
    });
    script::addNativeField(element, "name", nameField());
    script::addNativeField(element, "visible", visibleField());

    script::addScriptBase(text, element);
    script::addScriptBase(button, element);
    script::addScriptBase(image, element);
    script::addScriptBase(border, element);

    script::addNativeField(text, "text", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIText>(self, ref, "text");
            return widget ? script::scriptMake(widget->getText()) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            if (auto* widget = liveAs<UIText>(self, ref, "text")) {
                widget->setText(script::scriptToString(value));
            }
        },
    });
    script::addNativeField(text, "color", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIText>(self, ref, "color");
            return widget ? script::scriptMake(widget->_color) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            auto* widget = liveAs<UIText>(self, ref, "color");
            const auto* color = script::scriptIf<glm::vec4>(value);
            if (widget && color) {
                widget->setColor(*color);
            }
        },
    });
    // Button chrome from a texture: the same nine-patch skins borders, applied
    // to every state fill so hover/press stay readable on the same texture.
    script::addMethod(button, "setFillImage", script::fnVariadic<UIButton&>(
        [](UIButton& button, script::ScriptArgs args) -> script::ScriptValue {
            if (args.empty()) {
                throw script::ScriptError("Button.setFillImage expects (path[, margin[, tint]])");
            }
            glm::vec4 margin{12.0f, 12.0f, 12.0f, 12.0f};
            if (args.size() > 1) {
                if (const auto* value = script::scriptIf<glm::vec4>(args[1])) {
                    margin = *value;
                }
            }
            glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
            if (args.size() > 2) {
                if (const auto* value = script::scriptIf<glm::vec4>(args[2])) {
                    tint = *value;
                }
            }
            const std::string path = script::scriptToString(args[0]);
            const auto shade = [&](float k) {
                return glm::vec4(tint.x * k, tint.y * k, tint.z * k, tint.w);
            };
            button.setStyleField("normalFill", FBrush::ninePatch(path, margin, tint));
            button.setStyleField("hoveredFill", FBrush::ninePatch(path, margin, shade(1.25f)));
            button.setStyleField("pressedFill", FBrush::ninePatch(path, margin, shade(0.8f)));
            button.setStyleField("focusedFill", FBrush::ninePatch(path, margin, tint));
            button.setStyleField("disabledFill", FBrush::ninePatch(path, margin, shade(0.55f)));
            return {};
        }));

    script::addNativeField(text, "fontSize", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIText>(self, ref, "fontSize");
            return widget ? script::scriptMake(static_cast<int64_t>(widget->_fontSize)) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            if (auto* widget = liveAs<UIText>(self, ref, "fontSize")) {
                const int64_t size = script::scriptToInteger(value);
                if (size >= 0) {
                    widget->setFontSize(static_cast<uint32_t>(size));
                }
            }
        },
    });

    script::addNativeField(button, "onClicked", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            // A detached button still yields an event; add/remove no-op and warn once.
            (void)requireLive(self, ref, "onClicked");
            return script::scriptMake(makeButtonClickedRef(ref.a));
        },
    });
    script::addNativeField(button, "enabled", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIButton>(self, ref, "enabled");
            return widget ? script::scriptMake(widget->isEnabled()) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            if (auto* widget = liveAs<UIButton>(self, ref, "enabled")) {
                widget->setEnabled(script::scriptToBool(value));
            }
        },
    });

    script::addNativeField(image, "texture", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIImage>(self, ref, "texture");
            return widget ? script::scriptMake(widget->_assetPath) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            if (auto* widget = liveAs<UIImage>(self, ref, "texture")) {
                widget->setAssetPath(script::scriptToString(value));
            }
        },
    });

    script::addNativeField(border, "fill", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef& ref) -> script::ScriptValue {
            auto* widget = liveAs<UIBorder>(self, ref, "fill");
            return widget ? script::scriptMake(widget->getColor()) : script::ScriptValue{};
        },
        .write = [](void* self, const script::ScriptRef& ref, const script::ScriptValue& value) {
            auto* widget = liveAs<UIBorder>(self, ref, "fill");
            const auto* color = script::scriptIf<glm::vec4>(value);
            if (widget && color) {
                widget->setColor(*color);
            }
        },
    });
    // Styled chrome from a texture: nine-slice so the border survives any
    // panel size (Kenney UI Pack panels). `margin` is the texture-pixel
    // inset (left, top, right, bottom); `tint` multiplies the texture.
    // Falls back to an all-12px inset, which fits the 64px Kenney buttons.
    script::addMethod(border, "setFillImage", script::fnVariadic<UIBorder&>(
        [](UIBorder& border, script::ScriptArgs args) -> script::ScriptValue {
            if (args.empty()) {
                throw script::ScriptError("Border.setFillImage expects (path[, margin[, tint]])");
            }
            glm::vec4 margin{12.0f, 12.0f, 12.0f, 12.0f};
            if (args.size() > 1) {
                if (const auto* value = script::scriptIf<glm::vec4>(args[1])) {
                    margin = *value;
                }
            }
            glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
            if (args.size() > 2) {
                if (const auto* value = script::scriptIf<glm::vec4>(args[2])) {
                    tint = *value;
                }
            }
            border.setStyleField("fillColor",
                                 FBrush::ninePatch(script::scriptToString(args[0]), margin, tint));
            return {};
        }));
}

} // namespace ya
