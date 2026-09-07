#pragma once

// ============================================================================
// Path-keyed async texture catalog (GUI closure).
//
// Widgets never call AssetManager. Paint binds a path: Ready returns the
// strong Texture the snapshot holds through queue submit; Pending kicks
// IGuiTextureSource::requestLoad once; Failed stays Failed (no per-frame
// retry). Subscription is the existing paint-dependent Reactive revision —
// notify() only markPaintDirty widgets that read that path.
// ============================================================================

#include "Core/Api.h"
#include "GUI/Binding/Reactive.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace ya
{

struct Texture;

enum class EGuiTextureState : uint8_t
{
    Pending,
    Ready,
    Failed,
};

struct FGuiTextureLookup
{
    std::shared_ptr<Texture> texture;
    EGuiTextureState         state = EGuiTextureState::Pending;
};

using FGuiTextureReady =
    std::function<void(const std::string& path, FGuiTextureLookup lookup)>;
using FGuiTextureResolver =
    std::function<std::shared_ptr<Texture>(const std::string& assetPath)>;

/// Where `FGuiTextureReady` is allowed to run. Catalog notify rejects any
/// other thread before writing cache or Reactive dependents. There is no GUI
/// task queue: a deferred source must dispatch with its product scheduler
/// (AssetManager::dispatchToGameThread today).
enum class EGuiTextureCompletionThread : uint8_t
{
    /// `ready` runs before `requestLoad` returns, on the calling thread
    /// (already the WidgetTree owner / UI mutation thread).
    Caller,
    /// `ready` is deferred. The adapter must hop to the WidgetTree owner
    /// thread before invoking it.
    UIOwner,
};

/// Product / host adapter. Lives in GameRuntime or GUIApp; this header must
/// not include AssetManager. `epoch()` is a hot-reload token (0 = unused).
/// `FGuiTextureReady` must execute on the WidgetTree owner thread.
struct YA_GUI_API IGuiTextureSource
{
    virtual ~IGuiTextureSource() = default;

    [[nodiscard]] virtual FGuiTextureLookup lookup(const std::string& path) = 0;
    virtual void requestLoad(const std::string& path, FGuiTextureReady ready) = 0;
    [[nodiscard]] virtual uint64_t epoch() const { return 0; }
    /// Default is deferred UI-owner completion. Sync file loaders override
    /// `Caller`. GameRuntime's AssetManager adapter keeps the default because
    /// `onReady` is always `dispatchToGameThread`.
    [[nodiscard]] virtual EGuiTextureCompletionThread completionThread() const
    {
        return EGuiTextureCompletionThread::UIOwner;
    }
};

/// Tree-owned catalog. Entry addresses are stable (unique_ptr) so Reactive
/// paint dependents survive unordered_map rehash.
class YA_GUI_API FGuiTextureCatalog
{
public:
    FGuiTextureCatalog();

    void setSource(IGuiTextureSource* source) { _source = source; }
    [[nodiscard]] IGuiTextureSource* getSource() const { return _source; }
    /// Thread that constructed this catalog (WidgetTree ctor / test thread).
    /// Completions and cache writes must stay on this thread.
    [[nodiscard]] std::thread::id ownerThread() const { return _ownerThread; }
    [[nodiscard]] uint64_t foreignThreadCompletions() const { return _foreignThreadCompletions; }

    /// Subscribe the current paint widget to `path` and return the lookup.
    /// Ready/Failed cache hits do not call requestLoad. A miss asks the source
    /// (or `fallbackResolver` when no source is mounted). Never `set()`s a
    /// revision during paint — ready/fail notifications happen on notify().
    [[nodiscard]] FGuiTextureLookup bind(const std::string& path,
                                         const FGuiTextureResolver& fallbackResolver);

    void notify(const std::string& path, FGuiTextureLookup lookup);
    void invalidate(const std::string& path);
    void invalidateAll();
    /// Hot-reload: re-lookup every cached path. Ready hits keep the entry (no
    /// placeholder flash); missing/pending paths drop and re-kick.
    void refreshFromSource();
    /// Drop Ready/Failed/kicked without notifying paint dependents. Used when
    /// WidgetTree already dropped both item caches (resolver identity swap).
    void dropCachedLookups();

private:
    struct Entry
    {
        Reactive<uint64_t> revision{0};
        FGuiTextureLookup  cached;
        bool               bKicked = false;
    };

    [[nodiscard]] Entry& ensureEntry(const std::string& path);

    [[nodiscard]] bool onOwnerThread() const;
    /// Logs and counts a foreign-thread attempt. Returns true when the caller
    /// must return without mutating cache or dependents.
    bool rejectForeignThread(const char* op);

    IGuiTextureSource* _source = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Entry>> _entries;
    std::shared_ptr<uint8_t> _alive = std::make_shared<uint8_t>(1);
    std::thread::id          _ownerThread{};
    uint64_t                 _foreignThreadCompletions = 0;
};

} // namespace ya
