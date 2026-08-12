#include "Core/Application/GuiEventDriver.h"

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
            if (obj.contains("checkpoint")) {
                step.kind = EGuiScenarioStepKind::Checkpoint;
                step.tag  = obj["checkpoint"].get<std::string>();
                steps.push_back(std::move(step));
                continue;
            }
            if (obj.contains("drag")) {
                const auto& d     = obj["drag"];
                const auto  from  = d["from"];
                const auto  to    = d["to"];
                step.kind        = EGuiScenarioStepKind::Drag;
                step.point       = {from[0].get<float>(), from[1].get<float>()};
                step.dragTo      = {to[0].get<float>(), to[1].get<float>()};
                step.dragSteps   = d.value("steps", 8);
                steps.push_back(std::move(step));
                continue;
            }

            const std::string type = obj.value("event", "");
            step.point = {numberOrDefault(obj, "x", 0.0f), numberOrDefault(obj, "y", 0.0f)};
            if (type == "mouse_move") {
                step.kind = EGuiScenarioStepKind::MouseMove;
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

GuiScenarioExecutor::GuiScenarioExecutor(IGuiEventSink& sink,
                                         StepFrameFn   stepFrame,
                                         CheckpointFn  onCheckpoint)
    : _sink(sink)
    , _stepFrame(std::move(stepFrame))
    , _onCheckpoint(std::move(onCheckpoint))
{
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
        case EGuiScenarioStepKind::MouseMove:
            _sink.dispatch(MouseMoveEvent(step.point.x, step.point.y), step.point);
            break;
        case EGuiScenarioStepKind::MousePress:
            _sink.dispatch(MouseButtonPressedEvent(step.button), step.point);
            break;
        case EGuiScenarioStepKind::MouseRelease:
            _sink.dispatch(MouseButtonReleasedEvent(step.button), step.point);
            break;
        case EGuiScenarioStepKind::MouseWheel: {
            MouseScrolledEvent ev(step.wheel.x, step.wheel.y);
            _sink.dispatch(ev, step.point);
            break;
        }
        case EGuiScenarioStepKind::KeyPress: {
            KeyPressedEvent ev;
            ev._keyCode = step.key;
            ev._mod     = 0;
            ev.bRepeat  = false;
            _sink.dispatch(ev, {-1.0f, -1.0f});
            break;
        }
        case EGuiScenarioStepKind::KeyRelease: {
            KeyReleasedEvent ev;
            ev._keyCode = step.key;
            ev._mod     = 0;
            _sink.dispatch(ev, {-1.0f, -1.0f});
            break;
        }
        case EGuiScenarioStepKind::KeyTyped: {
            KeyTypedEvent ev(step.text);
            ev._mod = 0;
            _sink.dispatch(ev, {-1.0f, -1.0f});
            break;
        }
        case EGuiScenarioStepKind::Drag: {
            _sink.dispatch(MouseButtonPressedEvent(step.button), step.point);
            const int n = std::max(1, step.dragSteps);
            for (int i = 1; i <= n; ++i) {
                const float     t = static_cast<float>(i) / static_cast<float>(n);
                const glm::vec2 p = step.point + (step.dragTo - step.point) * t;
                _sink.dispatch(MouseMoveEvent(p.x, p.y), p);
            }
            _sink.dispatch(MouseButtonReleasedEvent(step.button), step.dragTo);
            break;
        }
        case EGuiScenarioStepKind::Checkpoint:
            if (_onCheckpoint) {
                _onCheckpoint(step.tag);
            }
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
