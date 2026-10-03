#pragma once

#include "Core/Api.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct EditorLayer;

/// Opens one content-browser file. `utf8Path` is the filesystem path as
/// stored; the opener decides whether to turn it into a VFS path.
using EditorContentOpener = std::function<void(EditorLayer& layer, std::string utf8Path)>;

struct FEditorContentOpener
{
    std::string          extension;
    EditorContentOpener  open;
};

/// Extension → content opener. The key is a file suffix such as ".scene.json".
/// Lookup is the longest case-insensitive match. `get()` is the process-wide
/// table the content browser queries; it is seeded with the built-in openers,
/// so another module can append or replace a suffix without editing that tab.
class YA_GAME_EDITOR_API EditorContentOpenerRegistry
{
    std::vector<FEditorContentOpener> _openers;

  public:
    static EditorContentOpenerRegistry& get();

    /// Replace the opener for this suffix, or append it. An empty suffix or
    /// an empty opener is ignored. The suffix may omit the leading dot.
    void registerOpener(std::string extension, EditorContentOpener opener);

    /// Longest registered suffix of `utf8Path`, or nullptr. The pointer is
    /// invalidated by the next registerOpener that grows the table.
    [[nodiscard]] const EditorContentOpener* find(std::string_view utf8Path) const;

    [[nodiscard]] const std::vector<FEditorContentOpener>& all() const { return _openers; }
};

/// .scene.json, .lua, .yaui.json, .mat and .material. Does not clear openers
/// already registered for other suffixes.
YA_GAME_EDITOR_API void registerBuiltinContentOpeners(EditorContentOpenerRegistry& registry);

} // namespace ya
