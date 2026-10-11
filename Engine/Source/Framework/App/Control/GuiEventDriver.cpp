#include "App/Control/GuiEventDriver.h"

#include "Core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace ya
{

namespace
{

std::unordered_map<std::string, EKey::T> buildKeyNameMap()
{
    std::unordered_map<std::string, EKey::T> map;
    for (int i = 0; i < 26; ++i) {
        map[std::string(1, static_cast<char>('A' + i))] = static_cast<EKey::T>(EKey::K_A + i);
    }
    for (int i = 0; i < 10; ++i) {
        map[std::string(1, static_cast<char>('0' + i))] = static_cast<EKey::T>(EKey::K_0 + i);
    }
    map["Space"]     = EKey::Space;
    map["Enter"]     = EKey::Enter;
    map["Escape"]    = EKey::Escape;
    map["Backspace"] = EKey::Backspace;
    map["Tab"]       = EKey::Tab;
    map["Shift"]     = EKey::LShift;
    map["Ctrl"]      = EKey::LCtrl;
    map["Alt"]       = EKey::LAlt;
    map["Up"]        = EKey::Up;
    map["Down"]      = EKey::Down;
    map["Left"]      = EKey::Left;
    map["Right"]     = EKey::Right;
    map["Insert"]    = EKey::Insert;
    map["Delete"]    = EKey::Delete;
    map["Home"]      = EKey::Home;
    map["End"]       = EKey::End;
    map["PageUp"]    = EKey::Pageup;
    map["PageDown"]  = EKey::PagedowN;
    for (int i = 1; i <= 12; ++i) {
        map["F" + std::to_string(i)] = static_cast<EKey::T>(EKey::F1 + i - 1);
    }
    return map;
}

float numberOrDefault(const nlohmann::json& obj, const char* key, float fallback)
{
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<float>();
}

/// "at" is either "center" (default) or a normalized [fx, fy] pair.
bool parseAnchor(const nlohmann::json& parent, const char* key, glm::vec2& outAnchor, std::string* errorOut, int lineNo)
{
    const auto it = parent.find(key);
    if (it == parent.end() || it->is_null()) {
        return true;
    }
    if (it->is_string()) {
        if (it->get<std::string>() != "center") {
            if (errorOut) {
                *errorOut = std::string("'") + key + "' must be \"center\" or [fx,fy] at line " + std::to_string(lineNo);
            }
            return false;
        }
        outAnchor = {0.5f, 0.5f};
        return true;
    }
    if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number()) {
        const float fx = (*it)[0].get<float>();
        const float fy = (*it)[1].get<float>();
        if (fx < 0.0f || fx > 1.0f || fy < 0.0f || fy > 1.0f) {
            if (errorOut) {
                *errorOut = std::string("'") + key + "' normalized coordinates must be within [0,1] at line " + std::to_string(lineNo);
            }
            return false;
        }
        outAnchor = {fx, fy};
        return true;
    }
    if (errorOut) {
        *errorOut = std::string("'") + key + "' must be \"center\" or [fx,fy] at line " + std::to_string(lineNo);
    }
    return false;
}

bool parseWidgetTarget(const nlohmann::json& parent,
                       std::string& outWidget,
                       std::string& outPart,
                       glm::vec2&   outAnchor,
                       std::string* errorOut,
                       int          lineNo)
{
    const auto widgetIt = parent.find("widget");
    if (widgetIt == parent.end() || !widgetIt->is_string() || widgetIt->get<std::string>().empty()) {
        if (errorOut) {
            *errorOut = "widget target requires a non-empty 'widget' name at line " + std::to_string(lineNo);
        }
        return false;
    }
    outWidget = widgetIt->get<std::string>();
    const auto partIt = parent.find("part");
    if (partIt != parent.end() && partIt->is_string()) {
        outPart = partIt->get<std::string>();
    }
    return parseAnchor(parent, "at", outAnchor, errorOut, lineNo);
}

} // namespace

EKey::T keyFromName(std::string_view name)
{
    static const std::unordered_map<std::string, EKey::T> map = buildKeyNameMap();
    const auto it = map.find(std::string(name));
    return it == map.end() ? EKey::NONE : it->second;
}

std::vector<GuiScenarioStep> parseGuiScenario(std::string_view jsonl,
                                              std::string* errorOut)
{
    std::vector<GuiScenarioStep> steps;
    std::istringstream stream{std::string(jsonl)};
    std::string        line;
    size_t             lineNo = 0;
    while (std::getline(stream, line)) {
        ++lineNo;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        try {
            const nlohmann::json obj = nlohmann::json::parse(line);
            GuiScenarioStep     step;

            if (obj.contains("frame")) {
                step.kind  = EGuiScenarioStepKind::Frame;
                step.frame = obj["frame"].is_number_unsigned()
                                 ? obj["frame"].get<uint32_t>()
                                 : static_cast<uint32_t>(obj["frame"].get<int>());
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("window_size")) {
                const auto& size = obj["window_size"];
                step.kind   = EGuiScenarioStepKind::SetWindowSize;
                step.width  = size.value("width", 0u);
                step.height = size.value("height", 0u);
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("checkpoint")) {
                step.kind = EGuiScenarioStepKind::Checkpoint;
                step.tag  = obj["checkpoint"].get<std::string>();
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("assert_validation_clean")) {
                step.kind = EGuiScenarioStepKind::AssertValidationClean;
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("assert")) {
                if (!obj["assert"].is_object()) {
                    if (errorOut) {
                        *errorOut = "assert must be an object at line " + std::to_string(lineNo);
                    }
                    return steps;
                }
                step.kind      = EGuiScenarioStepKind::Assert;
                step.assertion = obj["assert"].dump();
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("click")) {
                const auto& c = obj["click"];
                if (!c.is_object() || !parseWidgetTarget(c, step.widget, step.widgetPart, step.anchor, errorOut, lineNo)) {
                    if (errorOut && errorOut->empty()) {
                        *errorOut = "click must be an object with a widget target at line " + std::to_string(lineNo);
                    }
                    return steps;
                }
                step.kind   = EGuiScenarioStepKind::WidgetClick;
                step.button = c.value("button", 0);
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("wheel")) {
                const auto& w = obj["wheel"];
                if (!w.is_object() || !parseWidgetTarget(w, step.widget, step.widgetPart, step.anchor, errorOut, lineNo)) {
                    if (errorOut && errorOut->empty()) {
                        *errorOut = "wheel must be an object with a widget target at line " + std::to_string(lineNo);
                    }
                    return steps;
                }
                step.kind  = EGuiScenarioStepKind::MouseWheel;
                step.wheel = {numberOrDefault(w, "dx", 0.0f), numberOrDefault(w, "dy", 0.0f)};
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("drag")) {
                const auto& d = obj["drag"];
                if (!d.is_object()) {
                    if (errorOut) {
                        *errorOut = "drag must be an object at line " + std::to_string(lineNo);
                    }
                    return steps;
                }
                step.kind      = EGuiScenarioStepKind::Drag;
                step.dragSteps = d.value("steps", 8);
                if (d.contains("widget")) {
                    if (!parseWidgetTarget(d, step.widget, step.widgetPart, step.anchor, errorOut, lineNo)) {
                        return steps;
                    }
                    if (d.contains("to_widget")) {
                        const auto& toWidget = d["to_widget"];
                        if (!toWidget.is_string() || toWidget.get<std::string>().empty()) {
                            if (errorOut) {
                                *errorOut = "drag to_widget must be a non-empty name at line " + std::to_string(lineNo);
                            }
                            return steps;
                        }
                        step.dragToWidget = toWidget.get<std::string>();
                        const auto toPartIt = d.find("to_part");
                        if (toPartIt != d.end() && toPartIt->is_string()) {
                            step.dragToPart = toPartIt->get<std::string>();
                        }
                        if (!parseAnchor(d, "to_at", step.dragToAnchor, errorOut, lineNo)) {
                            return steps;
                        }
                    }
                    else if (d.contains("by")) {
                        const auto& by = d["by"];
                        if (!by.is_array() || by.size() != 2 || !by[0].is_number() || !by[1].is_number()) {
                            if (errorOut) {
                                *errorOut = "drag 'by' must be [dx,dy] at line " + std::to_string(lineNo);
                            }
                            return steps;
                        }
                        step.dragBy = {by[0].get<float>(), by[1].get<float>()};
                    }
                    else {
                        if (errorOut) {
                            *errorOut = "widget drag requires 'by' or 'to_widget' at line " + std::to_string(lineNo);
                        }
                        return steps;
                    }
                    steps.push_back(std::move(step));
                    continue;
                }
                const auto from = d["from"];
                const auto to   = d["to"];
                step.point  = {from[0].get<float>(), from[1].get<float>()};
                step.dragTo = {to[0].get<float>(), to[1].get<float>()};
                steps.push_back(std::move(step));
                continue;
            }

            const std::string type = obj.value("event", "");
            step.point = {numberOrDefault(obj, "x", 0.0f), numberOrDefault(obj, "y", 0.0f)};
            if (type == "mouse_move") {
                step.kind = EGuiScenarioStepKind::MouseMove;
                if (obj.contains("widget") &&
                    !parseWidgetTarget(obj, step.widget, step.widgetPart, step.anchor, errorOut, lineNo)) {
                    return steps;
                }
            }
            else if (type == "mouse_press") {
                step.kind   = EGuiScenarioStepKind::MousePress;
                step.button = obj.value("button", 0);
            }
            else if (type == "mouse_release") {
                step.kind   = EGuiScenarioStepKind::MouseRelease;
                step.button = obj.value("button", 0);
            }
            else if (type == "mouse_wheel") {
                step.kind  = EGuiScenarioStepKind::MouseWheel;
                step.wheel = {numberOrDefault(obj, "dx", 0.0f), numberOrDefault(obj, "dy", 0.0f)};
            }
            else if (type == "key_press") {
                step.kind = EGuiScenarioStepKind::KeyPress;
                step.key  = keyFromName(obj.value("key", ""));
                if (obj.value("shift", false)) {
                    step.keyMod |= EKeyMod::Shift;
                }
                if (obj.value("primary", false)) {
#if defined(__APPLE__)
                    step.keyMod |= EKeyMod::LMeta;
#else
                    step.keyMod |= EKeyMod::LCtrl;
#endif
                }
            }
            else if (type == "key_release") {
                step.kind = EGuiScenarioStepKind::KeyRelease;
                step.key  = keyFromName(obj.value("key", ""));
            }
            else if (type == "key_typed") {
                step.kind = EGuiScenarioStepKind::KeyTyped;
                step.text = obj.value("text", "");
            }
            else {
                if (errorOut) {
                    *errorOut = "unknown event type at line " + std::to_string(lineNo);
                }
                return steps;
            }
            steps.push_back(std::move(step));
        }
        catch (const std::exception& e) {
            if (errorOut) {
                *errorOut = std::string("parse error at line ") + std::to_string(lineNo) + ": " + e.what();
            }
            return steps;
        }
    }
    return steps;
}

std::vector<GuiScenarioStep> loadGuiScenarioFile(const std::string& path,
                                                 std::string* errorOut)
{
    std::ifstream file(path);
    if (!file) {
        if (errorOut) {
            *errorOut = "cannot open scenario file: " + path;
        }
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseGuiScenario(buffer.str(), errorOut);
}

GuiScenarioExecutor::GuiScenarioExecutor(IGuiEventSink&  sink,
                                         StepFrameFn     stepFrame,
                                         CheckpointFn    onCheckpoint,
                                         AssertFn        onAssert,
                                         WidgetResolveFn onResolveWidget)
    : _sink(sink)
    , _stepFrame(std::move(stepFrame))
    , _onCheckpoint(std::move(onCheckpoint))
    , _onAssert(std::move(onAssert))
    , _onResolveWidget(std::move(onResolveWidget))
{
}

void emitGuiScenarioStep(IGuiEventSink& sink, const GuiScenarioStep& step)
{
    // Simulated monotonic clock: every emitted event gets a step-based
    // timestamp (10ms per step), so time-based logic (double-click) is
    // deterministic in scenario replay instead of depending on wall time.
    static uint64_t s_simClockMs = 0;
    s_simClockMs += 10;
    const uint64_t ts = s_simClockMs;
    const auto stamp = [ts](Event& ev) { ev.setTimestampMs(ts); };

    switch (step.kind) {
    case EGuiScenarioStepKind::MouseMove: {
        MouseMoveEvent ev(step.point.x, step.point.y);
        stamp(ev);
        sink.dispatch(ev, step.point);
        break;
    }
    case EGuiScenarioStepKind::MousePress: {
        MouseMoveEvent moveEv(step.point.x, step.point.y);
        MouseButtonPressedEvent pressEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        stamp(moveEv);
        stamp(pressEv);
        sink.dispatch(moveEv, step.point);
        sink.dispatch(pressEv, step.point);
        break;
    }
    case EGuiScenarioStepKind::MouseRelease: {
        MouseMoveEvent moveEv(step.point.x, step.point.y);
        MouseButtonReleasedEvent releaseEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        stamp(moveEv);
        stamp(releaseEv);
        sink.dispatch(moveEv, step.point);
        sink.dispatch(releaseEv, step.point);
        break;
    }
    case EGuiScenarioStepKind::MouseWheel: {
        MouseMoveEvent moveEv(step.point.x, step.point.y);
        MouseScrolledEvent ev(step.wheel.x, step.wheel.y);
        stamp(moveEv);
        stamp(ev);
        sink.dispatch(moveEv, step.point);
        sink.dispatch(ev, step.point);
        break;
    }
    case EGuiScenarioStepKind::KeyPress: {
        KeyPressedEvent ev;
        ev._keyCode = step.key;
        ev._mod     = step.keyMod;
        ev.bRepeat  = false;
        stamp(ev);
        sink.dispatch(ev, {-1.0f, -1.0f});
        break;
    }
    case EGuiScenarioStepKind::KeyRelease: {
        KeyReleasedEvent ev;
        ev._keyCode = step.key;
        ev._mod     = step.keyMod;
        stamp(ev);
        sink.dispatch(ev, {-1.0f, -1.0f});
        break;
    }
    case EGuiScenarioStepKind::KeyTyped: {
        KeyTypedEvent ev(step.text);
        ev._mod = 0;
        stamp(ev);
        sink.dispatch(ev, {-1.0f, -1.0f});
        break;
    }
    case EGuiScenarioStepKind::Drag: {
        MouseMoveEvent moveEv0(step.point.x, step.point.y);
        MouseButtonPressedEvent pressEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        stamp(moveEv0);
        stamp(pressEv);
        sink.dispatch(moveEv0, step.point);
        sink.dispatch(pressEv, step.point);
        const int n = std::max(1, step.dragSteps);
        for (int i = 1; i <= n; ++i) {
            const float     t = static_cast<float>(i) / static_cast<float>(n);
            const glm::vec2 p = step.point + (step.dragTo - step.point) * t;
            MouseMoveEvent moveEv(p.x, p.y);
            stamp(moveEv);
            sink.dispatch(moveEv, p);
        }
        MouseButtonReleasedEvent releaseEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        stamp(releaseEv);
        sink.dispatch(releaseEv, step.dragTo);
        break;
    }
    case EGuiScenarioStepKind::WidgetClick: {
        // A click is one atomic gesture: bootstrap the cursor, press, release.
        // The point comes from the host's widget resolution (step.point).
        MouseMoveEvent          moveEv(step.point.x, step.point.y);
        MouseButtonPressedEvent  pressEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        MouseButtonReleasedEvent releaseEv(step.button == 1 ? EMouse::Right : EMouse::Left);
        stamp(moveEv);
        stamp(pressEv);
        stamp(releaseEv);
        sink.dispatch(moveEv, step.point);
        sink.dispatch(pressEv, step.point);
        sink.dispatch(releaseEv, step.point);
        break;
    }
    case EGuiScenarioStepKind::Frame:
    case EGuiScenarioStepKind::SetWindowSize:
    case EGuiScenarioStepKind::Checkpoint:
    case EGuiScenarioStepKind::Assert:
    case EGuiScenarioStepKind::AssertValidationClean:
        break;
    }
}

bool GuiScenarioExecutor::run(const std::vector<GuiScenarioStep>& steps)
{
    for (const GuiScenarioStep& step : steps) {
        switch (step.kind) {
        case EGuiScenarioStepKind::Frame:
            if (_stepFrame) {
                _stepFrame(step.frame ? step.frame : 1);
            }
            break;
        case EGuiScenarioStepKind::SetWindowSize:
            break;
        case EGuiScenarioStepKind::MouseMove:
        case EGuiScenarioStepKind::MousePress:
        case EGuiScenarioStepKind::MouseRelease:
        case EGuiScenarioStepKind::MouseWheel:
        case EGuiScenarioStepKind::KeyPress:
        case EGuiScenarioStepKind::KeyRelease:
        case EGuiScenarioStepKind::KeyTyped:
        case EGuiScenarioStepKind::Drag:
        case EGuiScenarioStepKind::WidgetClick: {
            GuiScenarioStep resolved = step;
            if (resolved.bWidgetTargeted()) {
                if (!_onResolveWidget) {
                    YA_CORE_ERROR("GuiScenarioExecutor: step targets widget '{}' but no widget resolver is installed",
                                  resolved.widget);
                    return false;
                }
                std::string error;
                if (!_onResolveWidget(resolved, resolved.point, resolved.dragTo, error)) {
                    YA_CORE_ERROR("GuiScenarioExecutor: widget target '{}' failed: {}",
                                  resolved.widget,
                                  error);
                    return false;
                }
                if (resolved.kind == EGuiScenarioStepKind::Drag && resolved.dragToWidget.empty()) {
                    resolved.dragTo = resolved.point + resolved.dragBy;
                }
            }
            emitGuiScenarioStep(_sink, resolved);
            break;
        }
        case EGuiScenarioStepKind::Checkpoint:
            if (_onCheckpoint) {
                _onCheckpoint(step.tag);
            }
            break;
        case EGuiScenarioStepKind::Assert:
            if (_onAssert && !_onAssert(step.assertion)) {
                return false;
            }
            break;
        case EGuiScenarioStepKind::AssertValidationClean:
            break;
        }
    }
    return true;
}

bool GuiScenarioExecutor::runJsonl(std::string_view jsonl)
{
    std::string error;
    const auto  steps = parseGuiScenario(jsonl, &error);
    if (!error.empty()) {
        YA_CORE_ERROR("GuiScenarioExecutor parse failed: {}", error);
        return false;
    }
    return run(steps);
}

} // namespace ya
