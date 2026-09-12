#pragma once

#include "GameEditor/UI/EditorWindowSession.h"

#include <memory>
#include <vector>

namespace ya
{

/// Default window plus extras created by MW-401 / MW-704. Extra sessions may
/// adopt a coordinator WidgetTree; the product path still ticks/presents only
/// the default native window. Extras must not share that window's WidgetTree.
/// Do not copy `IRender` per session.
struct EditorWindowRegistry
{
private:
    EditorWindowSession _default{kDefaultEditorWindowId};
    std::vector<std::unique_ptr<EditorWindowSession>> _extras;

public:
    [[nodiscard]] EditorWindowSession& defaultSession() { return _default; }
    [[nodiscard]] const EditorWindowSession& defaultSession() const { return _default; }

    [[nodiscard]] EditorWindowSession* find(EditorWindowId id)
    {
        if (id == _default.windowId()) {
            return &_default;
        }
        for (const std::unique_ptr<EditorWindowSession>& extra : _extras) {
            if (extra && extra->windowId() == id) {
                return extra.get();
            }
        }
        return nullptr;
    }
    [[nodiscard]] const EditorWindowSession* find(EditorWindowId id) const
    {
        if (id == _default.windowId()) {
            return &_default;
        }
        for (const std::unique_ptr<EditorWindowSession>& extra : _extras) {
            if (extra && extra->windowId() == id) {
                return extra.get();
            }
        }
        return nullptr;
    }

    EditorWindowSession* create(EditorWindowId id)
    {
        if (id == kInvalidEditorWindowId || find(id) != nullptr) {
            return nullptr;
        }
        _extras.push_back(std::make_unique<EditorWindowSession>(id));
        return _extras.back().get();
    }

    [[nodiscard]] size_t extraCount() const { return _extras.size(); }

    bool destroy(EditorWindowId id)
    {
        if (id == _default.windowId()) {
            return false;
        }
        for (auto it = _extras.begin(); it != _extras.end(); ++it) {
            if (*it && (*it)->windowId() == id) {
                (*it)->shutdown();
                _extras.erase(it);
                return true;
            }
        }
        return false;
    }

    template <typename Fn>
    void forEach(Fn&& fn)
    {
        fn(_default);
        for (std::unique_ptr<EditorWindowSession>& extra : _extras) {
            if (extra) {
                fn(*extra);
            }
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        fn(_default);
        for (const std::unique_ptr<EditorWindowSession>& extra : _extras) {
            if (extra) {
                fn(*extra);
            }
        }
    }
};

} // namespace ya
