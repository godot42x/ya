#include "GUI/Binding/LocalizedText.h"

namespace ya
{

namespace
{
LocalizationManager& manager()
{
    return LocalizationManager::get();
}
} // namespace

LocalizedText::State::~State()
{
    if (listenerHandle != 0) {
        manager().removeLanguageChangedListener(listenerHandle);
    }
}

std::string LocalizedText::State::resolveNow() const
{
    const std::string text = manager().tr(key);
    if (args.empty()) {
        return text;
    }
    return LocalizationManager::format(text, args);
}

void LocalizedText::State::subscribe()
{
    if (listenerHandle != 0) {
        return;
    }
    // Weak: the manager holds the callback for the process lifetime, so a
    // strong capture would keep this state (and its widget) alive forever.
    std::weak_ptr<State> weak = weak_from_this();
    listenerHandle = manager().addLanguageChangedListener([weak](std::string_view) {
        if (std::shared_ptr<State> state = weak.lock()) {
            if (state->value) {
                state->value->set(state->resolveNow());
            }
        }
    });
}

LocalizedText::LocalizedText(std::string key, std::vector<std::string> args)
{
    if (key.empty()) {
        return;
    }
    _state = std::make_shared<State>(std::move(key), std::move(args));
    _state->subscribe();
}

LocalizedText::~LocalizedText() = default;

const std::string& LocalizedText::key() const
{
    static const std::string kEmpty;
    return _state ? _state->key : kEmpty;
}

std::string LocalizedText::resolve() const
{
    return _state ? _state->resolveNow() : std::string{};
}

std::shared_ptr<Reactive<std::string>> LocalizedText::toReactive()
{
    if (!_state) {
        return nullptr;
    }
    if (!_state->value) {
        _state->value = std::make_shared<Reactive<std::string>>(_state->resolveNow());
    }
    return _state->value;
}

} // namespace ya
