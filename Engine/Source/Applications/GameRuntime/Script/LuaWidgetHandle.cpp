#include "LuaWidgetHandle.h"

#include "WidgetScriptRefs.h"

#include "Core/Delegate.h"
#include "Core/Log.h"
#include "Core/Scripting/ScriptBindings.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Text.h"
#include "Script/Lua/ScriptInstanceOps.h"

#include <string_view>

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

UIButton* liveButton(const script::ScriptRef& ref)
{
    auto* button = dynamic_cast<UIButton*>(resolveWidget(ref.a));
    if (!button || !widgetLive(button)) {
        warnWidgetGone(ref.a, "onClicked");
        return nullptr;
    }
    return button;
}

script::ScriptValue addClicked(void* /*self*/, const script::ScriptRef& ref, script::ScriptArgs args)
{
    UIButton* button = liveButton(ref);
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

script::ScriptValue removeClicked(void* /*self*/, const script::ScriptRef& ref, script::ScriptArgs args)
{
    UIButton* button = liveButton(ref);
    if (!button || args.empty()) {
        return script::scriptMake(false);
    }
    const int64_t handle = script::scriptToInteger(args[0]);
    if (handle <= 0) {
        return script::scriptMake(false);
    }
    return script::scriptMake(button->onClicked.remove(static_cast<DelegateHandle>(handle)));
}

script::ScriptValue removeAllClicked(void* /*self*/, const script::ScriptRef& ref, script::ScriptArgs args)
{
    UIButton* button = liveButton(ref);
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

void registerWidgetScriptSurface()
{
    const type_index_t element = type_index_v<UIElement>;
    const type_index_t text    = type_index_v<UIText>;
    const type_index_t button  = type_index_v<UIButton>;
    const type_index_t image   = type_index_v<UIImage>;
    const type_index_t border  = type_index_v<UIBorder>;
    const type_index_t timer   = type_index_v<WidgetScriptTimerTag>;
    const type_index_t clicked = type_index_v<WidgetScriptButtonClickedTag>;

    script::setScriptTypeName(element, "Widget");
    script::setScriptTypeName(text, "Text");
    script::setScriptTypeName(button, "Button");
    script::setScriptTypeName(image, "Image");
    script::setScriptTypeName(border, "Border");
    script::setScriptTypeName(timer, "UITimer");
    script::setScriptTypeName(clicked, "Event");

    script::addNativeField(element, "valid", script::ScriptNativeField{
        .read = [](void* self, const script::ScriptRef&) -> script::ScriptValue {
            return script::scriptMake(self && widgetLive(static_cast<const UIElement*>(self)));
        },
    });
    script::addNativeField(element, "name", nameField());
    script::addNativeField(element, "visible", visibleField());
    script::addNativeMethod(element, "find", [](void* self, const script::ScriptRef& ref, script::ScriptArgs args) -> script::ScriptValue {
        UIElement* widget = requireLive(self, ref, "find");
        GameUIHost* host = widget ? widgetHost(widget->getRuntimeId()) : nullptr;
        if (!widget || !host || args.empty()) {
            return {};
        }
        const std::string name = script::scriptToString(args[0]);
        UIElement* found = nullptr;
        if (!widget->isAttached()) {
            found = findInSubtree(*widget, name);
        }
        else if (UIElement* root = host->entryRootOf(*widget)) {
            found = host->findInEntry(*root, name).get();
        }
        return found ? widgetValue(*host, found->shared_from_this()) : script::ScriptValue{};
    });
    script::addNativeMethod(element, "destroy", [](void* self, const script::ScriptRef& ref, script::ScriptArgs) -> script::ScriptValue {
        if (UIElement* widget = requireLive(self, ref, "destroy")) {
            if (GameUIHost* host = widgetHost(widget->getRuntimeId())) {
                host->queueDestroy(*widget);
            }
        }
        return {};
    });
    script::addNativeMethod(element, "layout", [](void* self, const script::ScriptRef& ref, script::ScriptArgs) -> script::ScriptValue {
        if (UIElement* widget = requireLive(self, ref, "layout")) {
            if (GameUIHost* host = widgetHost(widget->getRuntimeId())) {
                host->layoutNow();
            }
        }
        return {};
    });
    script::addNativeMethod(element, "rect", [](void* self, const script::ScriptRef& ref, script::ScriptArgs) -> script::ScriptValue {
        UIElement* widget = requireLive(self, ref, "rect");
        if (!widget) {
            return {};
        }
        const Rect2D& rect = widget->getLayoutRect();
        return script::scriptMake(script::ScriptRect{
            .x = rect.pos.x,
            .y = rect.pos.y,
            .w = rect.extent.x,
            .h = rect.extent.y,
        });
    });

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

    script::addNativeMethod(timer, "cancel", [](void*, const script::ScriptRef& ref, script::ScriptArgs) -> script::ScriptValue {
        cancelWidgetTimer(ref);
        return {};
    });
    script::addNativeMethod(clicked,
                            "add",
                            &addClicked,
                            {type_index_v<script::ScriptSelfHandle>, type_index_v<script::ScriptFnHandle>});
    script::addNativeMethod(clicked, "remove", &removeClicked);
    script::addNativeMethod(clicked, "removeAll", &removeAllClicked, {type_index_v<script::ScriptSelfHandle>});
}

} // namespace ya
