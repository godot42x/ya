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

LocalizedText::State::State(std::string keyIn, std::vector<std::string> argsIn, std::string fallbackIn)
    : key(std::move(keyIn))
    , args(std::move(argsIn))
    , fallbackSource(std::move(fallbackIn))
{
}

LocalizedText::State::~State()
{
    if (listenerHandle != 0) {
        manager().removeLanguageChangedListener(listenerHandle);
    }
}

std::string LocalizedText::State::resolveNow() const
{
    const std::string text = manager().tr(key, fallbackSource);
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
    // strong capture would keep this state (and any bound widget) alive
    // forever. The subscription is dropped from ~State when the last owner
    // (handle or aliased reactive) is gone.
    std::weak_ptr<State> weak = weak_from_this();
    listenerHandle = manager().addLanguageChangedListener([weak](std::string_view) {
        if (std::shared_ptr<State> state = weak.lock()) {
            state->value.set(state->resolveNow());
        }
    });
}

LocalizedText::LocalizedText(std::string key, std::vector<std::string> args, std::string fallbackSource)
{
    if (key.empty()) {
        return;
    }
    _state = std::make_shared<State>(std::move(key), std::move(args), std::move(fallbackSource));
    _state->value.set(_state->resolveNow());
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
    // Aliasing constructor: the returned pointer shares ownership of the
    // State, so the subscription (and the reactive value) outlive this handle.
    return std::shared_ptr<Reactive<std::string>>(_state, &_state->value);
}

} // namespace ya
