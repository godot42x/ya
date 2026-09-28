#include "LuaWidgetHandle.h"

#include "Core/Log.h"

#include "ECS/Systems/LuaEvent.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/Text.h"

#include <optional>

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

} // namespace

bool LuaWidgetHandle::live() const
{
    UIElementRef locked = widget.lock();
    return locked && (locked->isAttached() || (host && host->isPendingSpawn(*locked)));
}

UIElement* LuaWidgetHandle::get(const char* operation) const
{
    if (live()) {
        return widget.lock().get();
    }
    if (!bWarned) {
        bWarned = true;
        YA_CORE_WARN("Lua widget handle: '{}' on a widget that is no longer in the UI; ignored", operation);
    }
    return nullptr;
}

sol::object makeLuaWidgetHandle(sol::state_view lua, GameUIHost& host, const UIElementRef& widget)
{
    if (!widget) {
        return sol::make_object(lua, sol::lua_nil);
    }
    const auto make = [&]<typename THandle>() {
        THandle handle;
        handle.widget = widget;
        handle.host   = &host;
        return sol::make_object(lua, std::move(handle));
    };
    UIElement* raw = widget.get();
    if (dynamic_cast<UIText*>(raw)) {
        return make.template operator()<LuaTextHandle>();
    }
    if (dynamic_cast<UIButton*>(raw)) {
        return make.template operator()<LuaButtonHandle>();
    }
    if (dynamic_cast<UIImage*>(raw)) {
        return make.template operator()<LuaImageHandle>();
    }
    if (dynamic_cast<UIBorder*>(raw)) {
        return make.template operator()<LuaBorderHandle>();
    }
    return make.template operator()<LuaWidgetHandle>();
}

void bindLuaWidgetHandles(sol::state& lua)
{
    const auto base = sol::base_classes;
    const auto bases = sol::bases<LuaWidgetHandle>();

    lua.new_usertype<LuaWidgetHandle>(
        "Widget",
        sol::no_constructor,
        "valid",
        sol::readonly_property([](const LuaWidgetHandle& h) { return h.live(); }),
        "name",
        sol::readonly_property([](const LuaWidgetHandle& h) -> std::optional<std::string> {
            if (UIElement* w = h.get("name")) {
                return w->_name;
            }
            return std::nullopt;
        }),
        "visible",
        sol::property(
            [](const LuaWidgetHandle& h) -> std::optional<bool> {
                if (UIElement* w = h.get("visible")) {
                    return w->isVisibleForRender();
                }
                return std::nullopt;
            },
            [](LuaWidgetHandle& h, bool bVisible) {
                if (UIElement* w = h.get("visible")) {
                    w->setVisibility(bVisible ? EWidgetVisibility::Visible : EWidgetVisibility::Hidden);
                }
            }),
        "find",
        [](const LuaWidgetHandle& h, const std::string& name, sol::this_state state) -> sol::object {
            UIElement* w = h.get("find");
            if (!w) {
                return sol::make_object(state, sol::lua_nil);
            }
            // A pending spawn is in no entry yet: search what it will bring.
            if (!w->isAttached()) {
                UIElement* found = findInSubtree(*w, name);
                return makeLuaWidgetHandle(state, *h.host, found ? found->shared_from_this() : nullptr);
            }
            UIElement* root = h.host->entryRootOf(*w);
            return makeLuaWidgetHandle(state, *h.host, root ? h.host->findInEntry(*root, name) : nullptr);
        },
        "destroy",
        [](const LuaWidgetHandle& h) {
            if (UIElement* w = h.get("destroy")) {
                h.host->queueDestroy(*w);
            }
        },
        "layout",
        [](const LuaWidgetHandle& h) {
            if (h.get("layout")) {
                h.host->layoutNow();
            }
        },
        "rect",
        [](const LuaWidgetHandle& h, sol::this_state state) -> sol::object {
            sol::state_view lua(state);
            UIElement* w = h.get("rect");
            if (!w) {
                return sol::make_object(lua, sol::lua_nil);
            }
            const Rect2D& rect = w->getLayoutRect();
            return sol::make_object(lua, lua.create_table_with("x", rect.pos.x, "y", rect.pos.y,
                                                               "w", rect.extent.x, "h", rect.extent.y));
        });

    lua.new_usertype<LuaTextHandle>(
        "Text",
        sol::no_constructor,
        base,
        bases,
        "text",
        sol::property(
            [](const LuaTextHandle& h) -> std::optional<std::string> {
                if (auto* text = h.as<UIText>("text")) {
                    return text->getText();
                }
                return std::nullopt;
            },
            [](LuaTextHandle& h, const std::string& value) {
                if (auto* text = h.as<UIText>("text")) {
                    text->setText(value);
                }
            }),
        "color",
        sol::property(
            [](const LuaTextHandle& h) -> std::optional<glm::vec4> {
                if (auto* text = h.as<UIText>("color")) {
                    return text->_color;
                }
                return std::nullopt;
            },
            [](LuaTextHandle& h, const glm::vec4& value) {
                if (auto* text = h.as<UIText>("color")) {
                    text->setColor(value);
                }
            }),
        "fontSize",
        sol::property(
            [](const LuaTextHandle& h) -> std::optional<uint32_t> {
                if (auto* text = h.as<UIText>("fontSize")) {
                    return text->_fontSize;
                }
                return std::nullopt;
            },
            [](LuaTextHandle& h, uint32_t value) {
                if (auto* text = h.as<UIText>("fontSize")) {
                    text->setFontSize(value);
                }
            }));

    lua.new_usertype<LuaButtonHandle>(
        "Button",
        sol::no_constructor,
        base,
        bases,
        "onClicked",
        sol::readonly_property([](const LuaButtonHandle& h) {
            auto* button = h.as<UIButton>("onClicked");
            return makeLuaEvent(button ? std::static_pointer_cast<UIButton>(button->shared_from_this()) : nullptr,
                                &UIButton::onClicked,
                                "Button.onClicked");
        }),
        "enabled",
        sol::property(
            [](const LuaButtonHandle& h) -> std::optional<bool> {
                if (auto* button = h.as<UIButton>("enabled")) {
                    return button->isEnabled();
                }
                return std::nullopt;
            },
            [](LuaButtonHandle& h, bool bEnabled) {
                if (auto* button = h.as<UIButton>("enabled")) {
                    button->setEnabled(bEnabled);
                }
            }));

    lua.new_usertype<LuaImageHandle>(
        "Image",
        sol::no_constructor,
        base,
        bases,
        "texture",
        sol::property(
            [](const LuaImageHandle& h) -> std::optional<std::string> {
                if (auto* image = h.as<UIImage>("texture")) {
                    return image->_assetPath;
                }
                return std::nullopt;
            },
            [](LuaImageHandle& h, const std::string& path) {
                if (auto* image = h.as<UIImage>("texture")) {
                    image->setAssetPath(path);
                }
            }));

    lua.new_usertype<LuaBorderHandle>(
        "Border",
        sol::no_constructor,
        base,
        bases,
        "fill",
        sol::property(
            [](const LuaBorderHandle& h) -> std::optional<glm::vec4> {
                if (auto* border = h.as<UIBorder>("fill")) {
                    return border->getColor();
                }
                return std::nullopt;
            },
            [](LuaBorderHandle& h, const glm::vec4& value) {
                if (auto* border = h.as<UIBorder>("fill")) {
                    border->setColor(value);
                }
            }));
}

} // namespace ya
