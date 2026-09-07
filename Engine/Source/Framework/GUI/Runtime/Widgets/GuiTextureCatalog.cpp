#include "GUI/Widgets/GuiTextureCatalog.h"

#include "Core/Log.h"

namespace ya
{

FGuiTextureCatalog::FGuiTextureCatalog()
    : _ownerThread(std::this_thread::get_id())
{
}

bool FGuiTextureCatalog::onOwnerThread() const
{
    return std::this_thread::get_id() == _ownerThread;
}

bool FGuiTextureCatalog::rejectForeignThread(const char* op)
{
    if (onOwnerThread()) {
        return false;
    }
    ++_foreignThreadCompletions;
    YA_CORE_ERROR("FGuiTextureCatalog::{} rejected from a non-UI owner thread", op);
    return true;
}

FGuiTextureCatalog::Entry& FGuiTextureCatalog::ensureEntry(const std::string& path)
{
    auto it = _entries.find(path);
    if (it == _entries.end()) {
        it = _entries.emplace(path, std::make_unique<Entry>()).first;
    }
    return *it->second;
}

FGuiTextureLookup FGuiTextureCatalog::bind(const std::string& path,
                                           const FGuiTextureResolver& fallbackResolver)
{
    if (rejectForeignThread("bind") || path.empty()) {
        return {};
    }

    Entry& entry = ensureEntry(path);
    (void)entry.revision.get();

    if (entry.cached.state == EGuiTextureState::Ready && entry.cached.texture) {
        return entry.cached;
    }
    if (entry.cached.state == EGuiTextureState::Failed) {
        return entry.cached;
    }

    if (_source) {
        const FGuiTextureLookup looked = _source->lookup(path);
        if (looked.state == EGuiTextureState::Ready && looked.texture) {
            entry.cached = looked;
            return entry.cached;
        }
        if (looked.state == EGuiTextureState::Failed) {
            entry.cached = looked;
            return entry.cached;
        }
        if (!entry.bKicked) {
            entry.bKicked = true;
            const std::weak_ptr<uint8_t> alive = _alive;
            _source->requestLoad(path, [this, alive](const std::string& readyPath,
                                                     FGuiTextureLookup result) {
                if (!alive.lock()) {
                    return;
                }
                notify(readyPath, std::move(result));
            });
        }
        if (entry.cached.state == EGuiTextureState::Ready ||
            entry.cached.state == EGuiTextureState::Failed) {
            // requestLoad completed inline (no game-thread sink).
            return entry.cached;
        }
        entry.cached = {nullptr, EGuiTextureState::Pending};
        return entry.cached;
    }

    if (fallbackResolver) {
        if (auto texture = fallbackResolver(path)) {
            entry.cached = {std::move(texture), EGuiTextureState::Ready};
            return entry.cached;
        }
    }
    entry.cached = {nullptr, EGuiTextureState::Pending};
    return entry.cached;
}

void FGuiTextureCatalog::notify(const std::string& path, FGuiTextureLookup lookup)
{
    if (rejectForeignThread("notify") || path.empty()) {
        return;
    }
    Entry& entry = ensureEntry(path);
    entry.cached = std::move(lookup);
    entry.bKicked = true;
    entry.revision.set(entry.revision.value() + 1);
}

void FGuiTextureCatalog::invalidate(const std::string& path)
{
    if (rejectForeignThread("invalidate")) {
        return;
    }
    auto it = _entries.find(path);
    if (it == _entries.end()) {
        return;
    }
    it->second->cached = {};
    it->second->bKicked = false;
    it->second->revision.set(it->second->revision.value() + 1);
}

void FGuiTextureCatalog::invalidateAll()
{
    if (rejectForeignThread("invalidateAll")) {
        return;
    }
    for (auto& [path, entry] : _entries) {
        (void)path;
        entry->cached = {};
        entry->bKicked = false;
        entry->revision.set(entry->revision.value() + 1);
    }
}

void FGuiTextureCatalog::refreshFromSource()
{
    if (rejectForeignThread("refreshFromSource")) {
        return;
    }
    if (!_source) {
        invalidateAll();
        return;
    }
    for (auto& [path, entry] : _entries) {
        const FGuiTextureLookup looked = _source->lookup(path);
        if (looked.state == EGuiTextureState::Ready && looked.texture) {
            if (entry->cached.state != EGuiTextureState::Ready ||
                entry->cached.texture != looked.texture) {
                entry->cached = looked;
                entry->bKicked = true;
                entry->revision.set(entry->revision.value() + 1);
            }
            continue;
        }
        if (looked.state == EGuiTextureState::Failed) {
            if (entry->cached.state != EGuiTextureState::Failed) {
                entry->cached = looked;
                entry->bKicked = true;
                entry->revision.set(entry->revision.value() + 1);
            }
            continue;
        }
        entry->cached = {};
        entry->bKicked = false;
        entry->revision.set(entry->revision.value() + 1);
    }
}

void FGuiTextureCatalog::dropCachedLookups()
{
    if (rejectForeignThread("dropCachedLookups")) {
        return;
    }
    for (auto& [path, entry] : _entries) {
        (void)path;
        entry->cached = {};
        entry->bKicked = false;
    }
}

} // namespace ya
