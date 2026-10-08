#pragma once

// Private to GameRuntime's widget script bindings. The public surface is
// `widgetScriptRef` in LuaWidgetHandle.h.

#include "Core/Scripting/ScriptValue.h"

#include <cstdint>
#include <memory>

namespace ya
{

struct GameUIHost;
struct UIElement;

/// Stable script objects for refs that are not widgets. The address is the
/// resolved `self`; the id the script used to know lives on the object, so a
/// method registered with `script::fn` does not need the `ScriptRef`.
/// `unique_ptr` slots keep the address valid across map rehash.
struct WidgetScriptTimer
{
    uint64_t token = 0;
};

struct WidgetScriptButtonClicked
{
    uint64_t widgetRuntimeId = 0;
};

void trackWidget(GameUIHost& host, const std::shared_ptr<UIElement>& widget);
[[nodiscard]] UIElement* resolveWidget(uint64_t runtimeId);
[[nodiscard]] bool       widgetLive(const UIElement* widget);
void                     warnWidgetGone(uint64_t runtimeId, const char* operation);
[[nodiscard]] GameUIHost* widgetHost(uint64_t runtimeId);

[[nodiscard]] uint32_t widgetRefKind();
[[nodiscard]] script::ScriptRef makeWidgetTimerRef(GameUIHost& host, uint64_t timerId);
[[nodiscard]] script::ScriptRef makeButtonClickedRef(uint64_t widgetRuntimeId);
void cancelWidgetTimer(const WidgetScriptTimer& timer);

void ensureWidgetScriptObjectsReflected();
void registerWidgetNativeMethods();
void registerWidgetScriptSurface();

} // namespace ya
