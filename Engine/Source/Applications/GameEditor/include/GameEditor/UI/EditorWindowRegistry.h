#pragma once

#include "GameEditor/UI/EditorWindowSession.h"

namespace ya
{

/// Single-element window table. ES-2 only stores the default editor window;
/// do not grow this into a multi-window map before ES-5.
struct EditorWindowRegistry
{
private:
    EditorWindowSession _default{kDefaultEditorWindowId};

public:
    [[nodiscard]] EditorWindowSession& defaultSession() { return _default; }
    [[nodiscard]] const EditorWindowSession& defaultSession() const { return _default; }

    [[nodiscard]] EditorWindowSession* find(EditorWindowId id)
    {
        return id == _default.windowId() ? &_default : nullptr;
    }
    [[nodiscard]] const EditorWindowSession* find(EditorWindowId id) const
    {
        return id == _default.windowId() ? &_default : nullptr;
    }
};

} // namespace ya
