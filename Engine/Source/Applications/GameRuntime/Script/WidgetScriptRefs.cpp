#include "WidgetScriptRefs.h"

#include "Core/Log.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"
#include "Core/TypeIndex.h"
#include "GUI/Widgets/UIElement.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include <unordered_map>
#include <unordered_set>

YA_REFLECT_BEGIN_EXTERNAL(ya::WidgetScriptTimer)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::WidgetScriptButtonClicked)
YA_REFLECT_END_EXTERNAL()

namespace ya
{
namespace
{

struct WidgetSlot
{
    std::weak_ptr<UIElement> widget;
    GameUIHost*              host = nullptr;
};

struct TimerSlot
{
    GameUIHost* host        = nullptr;
    uint64_t    hostTimerId = 0;
};

std::unordered_map<uint64_t, WidgetSlot> gWidgets;
std::unordered_map<uint64_t, TimerSlot>  gTimers;
std::unordered_map<uint64_t, std::unique_ptr<WidgetScriptTimer>>          gTimerObjects;
std::unordered_map<uint64_t, std::unique_ptr<WidgetScriptButtonClicked>>  gClickedObjects;
std::unordered_set<uint64_t>             gWarned;
uint64_t                                 gNextTimer = 1;

struct WidgetRefIds
{
    uint32_t widget  = 0;
    uint32_t timer   = 0;
    uint32_t clicked = 0;
};

const WidgetRefIds& ids()
{
    static const WidgetRefIds registered = [] {
        WidgetRefIds out;
        out.widget = script::registerRefKind(script::ScriptRefKind{
            .name         = "widget",
            .resolve      = [](const script::ScriptRef& ref) -> void* { return resolveWidget(ref.a); },
            .missingIsNil = true,
        });
        out.timer = script::registerRefKind(script::ScriptRefKind{
            .name         = "timer",
            .resolve      = [](const script::ScriptRef& ref) -> void* {
                const auto it = gTimerObjects.find(ref.a);
                return it == gTimerObjects.end() ? nullptr : it->second.get();
            },
            .missingIsNil = true,
        });
        out.clicked = script::registerRefKind(script::ScriptRefKind{
            .name    = "event",
            .resolve = [](const script::ScriptRef& ref) -> void* {
                const auto it = gClickedObjects.find(ref.a);
                return it == gClickedObjects.end() ? nullptr : it->second.get();
            },
        });
        registerWidgetScriptSurface();
        return out;
    }();
    return registered;
}

} // namespace

void ensureWidgetScriptObjectsReflected()
{
    ya::reflection::detail::ExternalReflect<WidgetScriptTimer>::__ensure_reflection_registered();
    ya::reflection::detail::ExternalReflect<WidgetScriptButtonClicked>::__ensure_reflection_registered();
}

void trackWidget(GameUIHost& host, const std::shared_ptr<UIElement>& widget)
{
    if (!widget) {
        return;
    }
    gWidgets.insert_or_assign(widget->getRuntimeId(), WidgetSlot{.widget = widget, .host = &host});
}

UIElement* resolveWidget(uint64_t runtimeId)
{
    const auto it = gWidgets.find(runtimeId);
    if (it == gWidgets.end()) {
        return nullptr;
    }
    const std::shared_ptr<UIElement> locked = it->second.widget.lock();
    if (!locked) {
        gWidgets.erase(it);
        return nullptr;
    }
    return locked.get();
}

bool widgetLive(const UIElement* widget)
{
    if (!widget) {
        return false;
    }
    GameUIHost* host = widgetHost(widget->getRuntimeId());
    return widget->isAttached() || (host && host->isPendingSpawn(*widget));
}

void warnWidgetGone(uint64_t runtimeId, const char* operation)
{
    if (!gWarned.insert(runtimeId).second) {
        return;
    }
    YA_CORE_WARN("Lua widget handle: '{}' on a widget that is no longer in the UI; ignored", operation);
}

GameUIHost* widgetHost(uint64_t runtimeId)
{
    const auto it = gWidgets.find(runtimeId);
    return it == gWidgets.end() ? nullptr : it->second.host;
}

uint32_t widgetRefKind()
{
    return ids().widget;
}

script::ScriptRef makeWidgetTimerRef(GameUIHost& host, uint64_t timerId)
{
    const uint64_t token = gNextTimer++;
    auto           object = std::make_unique<WidgetScriptTimer>();
    object->token         = token;
    gTimerObjects.insert_or_assign(token, std::move(object));
    gTimers.insert_or_assign(token, TimerSlot{.host = &host, .hostTimerId = timerId});
    return script::ScriptRef{
        .type = type_index_v<WidgetScriptTimer>,
        .kind = ids().timer,
        .a    = token,
        .b    = 0,
    };
}

script::ScriptRef makeButtonClickedRef(uint64_t widgetRuntimeId)
{
    if (widgetRuntimeId != 0 && !gClickedObjects.contains(widgetRuntimeId)) {
        auto object               = std::make_unique<WidgetScriptButtonClicked>();
        object->widgetRuntimeId  = widgetRuntimeId;
        gClickedObjects.emplace(widgetRuntimeId, std::move(object));
    }
    return script::ScriptRef{
        .type = type_index_v<WidgetScriptButtonClicked>,
        .kind = ids().clicked,
        .a    = widgetRuntimeId,
        .b    = 0,
    };
}

void cancelWidgetTimer(const WidgetScriptTimer& timer)
{
    const uint64_t token = timer.token;
    const auto     it    = gTimers.find(token);
    if (it != gTimers.end()) {
        if (it->second.host) {
            it->second.host->cancelTimer(it->second.hostTimerId);
        }
        gTimers.erase(it);
    }
    gTimerObjects.erase(token);
}

} // namespace ya
