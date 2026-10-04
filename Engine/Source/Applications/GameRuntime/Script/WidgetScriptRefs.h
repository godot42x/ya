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

/// Tag types for script refs that are not widgets. One definition, so
/// `type_index_v` agrees across this target's translation units.
struct WidgetScriptTimerTag {};
struct WidgetScriptButtonClickedTag {};

void trackWidget(GameUIHost& host, const std::shared_ptr<UIElement>& widget);
[[nodiscard]] UIElement* resolveWidget(uint64_t runtimeId);
[[nodiscard]] bool       widgetLive(const UIElement* widget);
void                     warnWidgetGone(uint64_t runtimeId, const char* operation);
[[nodiscard]] GameUIHost* widgetHost(uint64_t runtimeId);

[[nodiscard]] uint32_t widgetRefKind();
[[nodiscard]] script::ScriptRef makeWidgetTimerRef(GameUIHost& host, uint64_t timerId);
[[nodiscard]] script::ScriptRef makeButtonClickedRef(uint64_t widgetRuntimeId);
void cancelWidgetTimer(const script::ScriptRef& ref);

void registerWidgetScriptSurface();

} // namespace ya
