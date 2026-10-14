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

/// A key + optional substitution args that resolve to display text, and can be
/// bound as a reactive value. The reactive side re-resolves whenever
/// LocalizationManager's active language changes, so a `UIText::bindText`
/// consumer repaints without any panel knowing about localization.
///
/// Layering: this lives in the GUI binding layer, not in Core, because it owns
/// a `Reactive<std::string>`. Core (where LocalizationManager lives) must not
/// depend on the GUI binding layer.
///
/// Lifetime: copies share one `State` (key, args, the reactive value, and one
/// language subscription). `toReactive()` returns an aliasing shared_ptr that
/// co-owns that State, so the subscription stays alive as long as either the
/// handle OR the bound reactive is alive, and is dropped exactly once.
class YA_GUI_API LocalizedText
{
  public:
    LocalizedText() = default;

    /// `key` is looked up through LocalizationManager; `args` fill `{N}`
    /// placeholders in the translated result (see LocalizationManager::format).
    /// `fallbackSource` is the readable source string shown when the key has
    /// no translation (long keys use an identifier + this English source).
    explicit LocalizedText(std::string key,
                           std::vector<std::string> args = {},
                           std::string fallbackSource = {});
    ~LocalizedText();

    LocalizedText(const LocalizedText&)            = default;
    LocalizedText& operator=(const LocalizedText&) = default;
    LocalizedText(LocalizedText&&) noexcept        = default;
    LocalizedText& operator=(LocalizedText&&) noexcept = default;

    [[nodiscard]] bool empty() const { return !_state || _state->key.empty(); }
    [[nodiscard]] const std::string& key() const;

    /// Resolve against the current language right now (not the cached value).
    [[nodiscard]] std::string resolve() const;

    /// The reactive value backing this handle, kept up to date on every
    /// language change. Null only for an empty handle. Co-owns the handle
    /// state, so binding it outlives the handle object.
    [[nodiscard]] std::shared_ptr<Reactive<std::string>> toReactive();

  private:
    struct State : public std::enable_shared_from_this<State>
    {
        std::string              key;
        std::vector<std::string> args;
        std::string              fallbackSource;
        Reactive<std::string>    value;
        LocalizationManager::LanguageChangedHandle listenerHandle = 0;

        State(std::string keyIn, std::vector<std::string> argsIn, std::string fallbackIn);
        ~State();
        State(const State&)            = delete;
        State& operator=(const State&) = delete;

        [[nodiscard]] std::string resolveNow() const;
        /// Subscribe to language changes (idempotent).
        void subscribe();
    };

    std::shared_ptr<State> _state;
};

} // namespace ya
