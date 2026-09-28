#pragma once

// Lua handles to Game UI widgets. A handle is a weak reference: once its widget
// leaves the tree, reads return nil, writes do nothing, and the handle warns
// once. Each widget kind gets its own explicit handle type; nothing is bound
// through reflection.

#include "GUI/Widgets/UIElement.h"

#include <sol/sol.hpp>

#include <memory>

namespace ya
{

struct GameUIHost;

struct LuaWidgetHandle
{
    std::weak_ptr<UIElement> widget;
    GameUIHost*              host    = nullptr;
    mutable bool             bWarned = false;

    /// The live widget, or null (warning once) when it is gone.
    [[nodiscard]] UIElement* get(const char* operation) const;
    template <typename T>
    [[nodiscard]] T* as(const char* operation) const
    {
        return dynamic_cast<T*>(get(operation));
    }
};

struct LuaTextHandle : LuaWidgetHandle {};
struct LuaButtonHandle : LuaWidgetHandle {};
struct LuaImageHandle : LuaWidgetHandle {};
struct LuaBorderHandle : LuaWidgetHandle {};

/// The most specific handle for `widget`, or nil when it is null.
[[nodiscard]] sol::object makeLuaWidgetHandle(sol::state_view lua, GameUIHost& host, const UIElementRef& widget);

/// Register the handle usertypes (Widget, Text, Button, Image, Border).
void bindLuaWidgetHandles(sol::state& lua);

} // namespace ya
