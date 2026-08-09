#pragma once

// ============================================================================
// UITypeRegistry - stable type identity and explicit registration for Game UI
// widget types (ui-widget-tree-refactor Phase 1).
//
// Explicit registration (stable string typeId + factory) is the public
// mechanism, NOT reflection scanning: DLL load/unload order is uncontrolled,
// C++ renames must not break documents, and the registry must know which
// module owns each instance for the unload guard.
//
// Live-instance guard: instances created through createInstance() carry a
// module lease; endModule() refuses to unload a module that still has live
// instances. The registry is the single shared owner of type state (one DLL
// owns it in shared linkage, one TU in monolith).
// ============================================================================

#include "GUI/Widgets/UIElement.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Module ownership handle for UI widget types. Created via beginModule();
/// endModule() fails while live instances remain.
struct UITypeModule
{
    explicit UITypeModule(std::string id) : moduleId(std::move(id)) {}

    std::string moduleId;
    uint64_t    liveInstances = 0;
};

struct UITypeRegisterInfo
{
    std::string                  typeId;       // stable, e.g. "engine.button"
    std::string                  displayName;  // editor palette label
    std::string                  category;     // editor palette category
    std::shared_ptr<UITypeModule> module;       // optional module owner
};

class UITypeRegistry
{
  public:
    /// Single shared owner of registry state.
    static UITypeRegistry& instance();

    UITypeRegistry()                           = default;
    UITypeRegistry(const UITypeRegistry&)      = delete;
    UITypeRegistry& operator=(const UITypeRegistry&) = delete;

    // === Module ownership ===
    /// Begin a module scope. Returns a handle that types bind to; the module
    /// stays registered until endModule() succeeds.
    std::shared_ptr<UITypeModule> beginModule(const std::string& moduleId);
    /// End a module scope. Fails (logs an error, returns false) while any
    /// instance created from this module's types is still alive.
    bool endModule(const std::shared_ptr<UITypeModule>& module);

    // === Type registration ===
    /// Register a widget type with an explicit factory. Replaces any existing
    /// registration with the same typeId (after logging).
    void registerType(const UITypeRegisterInfo& info, std::function<UIElementRef()> factory);
    void unregisterType(const std::string& typeId);

    // === Instantiation ===
    /// Create a detached instance of `typeId` (no tree/parent). Attaches the
    /// module lease so live-instance tracking follows the widget lifetime.
    /// Returns nullptr for unknown type IDs (with a diagnostic log).
    UIElementRef createInstance(const std::string& typeId) const;

    // === Introspection ===
    [[nodiscard]] const UITypeRegisterInfo* findType(const std::string& typeId) const;
    /// All registered type IDs, sorted.
    [[nodiscard]] std::vector<std::string> getTypeIds() const;

    /// Register the engine built-in widget types (engine.panel/text/button/
    /// container). Called lazily by instance(); idempotent.
    void ensureBuiltinTypesRegistered();

  private:
    struct Entry
    {
        UITypeRegisterInfo             info;
        std::function<UIElementRef()>  factory;
        std::weak_ptr<UITypeModule>    module;
    };

    std::unordered_map<std::string, Entry>                       _types;
    std::unordered_map<std::string, std::shared_ptr<UITypeModule>> _modules;
    bool _bBuiltinsRegistered = false;
};

} // namespace ya
