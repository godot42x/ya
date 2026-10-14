#pragma once

#include "Core/Api.h"
#include "Core/Localization/LocalizationManager.h"
#include "GUI/Binding/Reactive.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

/// A key + optional substitution args that resolve to display text and can be
/// bound as a reactive value. The reactive side re-resolves whenever
/// LocalizationManager's active language changes, so a `UIText::bindText`
/// consumer repaints without any panel knowing about localization.
///
/// Layering: this lives in the GUI binding layer, not in Core, because it
/// owns a `Reactive<std::string>`. Core (where LocalizationManager lives) must
/// not depend on the GUI binding layer.
///
/// Copies share one underlying `State` (key, args, reactive value) and one
/// language subscription, so `bindText(handle.toReactive())` observes the same
/// value across copies and the subscription is dropped exactly once, when the
/// last copy dies.
class YA_GUI_API LocalizedText
{
  public:
    LocalizedText() = default;

    /// `key` is looked up through LocalizationManager; `args` fill `{N}`
    /// placeholders in the translated result (see LocalizationManager::format).
    explicit LocalizedText(std::string key, std::vector<std::string> args = {});
    ~LocalizedText();

    LocalizedText(const LocalizedText&)            = default;
    LocalizedText& operator=(const LocalizedText&) = default;
    LocalizedText(LocalizedText&&) noexcept        = default;
    LocalizedText& operator=(LocalizedText&&) noexcept = default;

    [[nodiscard]] bool empty() const { return !_state || _state->key.empty(); }
    [[nodiscard]] const std::string& key() const;

    /// Resolve against the current language right now.
    [[nodiscard]] std::string resolve() const;

    /// The shared reactive value backing this handle, created lazily and kept
    /// up to date on every language change. Null only for an empty handle.
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> toReactive();

  private:
    struct State : public std::enable_shared_from_this<State>
    {
        std::string              key;
        std::vector<std::string> args;
        std::shared_ptr<Reactive<std::string>> value;
        LocalizationManager::LanguageChangedHandle listenerHandle = 0;

        State(std::string keyIn, std::vector<std::string> argsIn)
            : key(std::move(keyIn)), args(std::move(argsIn))
        {
        }
        ~State();
        State(const State&)            = delete;
        State& operator=(const State&) = delete;

        [[nodiscard]] std::string resolveNow() const;
        /// Subscribe to language changes (idempotent). Fires the first refresh.
        void subscribe();
    };

    std::shared_ptr<State> _state;
};

} // namespace ya
