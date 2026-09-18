#pragma once

#include "GUI/Binding/UndoStack.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ya
{

enum class EEditorDocumentKind : uint8_t
{
    Scene,
    UI,
    Material,
    Script,
};

/// Stable identity for one edited document. `key` is a path, uiKey, or
/// untitled token; empty key is invalid. Two windows on the same id share
/// one session (singleton). This is not a Camera / PreviewTarget / present
/// surface.
struct FEditorDocumentId
{
    EEditorDocumentKind kind = EEditorDocumentKind::Scene;
    std::string         key;

    [[nodiscard]] bool valid() const { return !key.empty(); }
};

[[nodiscard]] inline bool operator==(const FEditorDocumentId& a, const FEditorDocumentId& b)
{
    return a.kind == b.kind && a.key == b.key;
}

[[nodiscard]] inline bool operator!=(const FEditorDocumentId& a, const FEditorDocumentId& b)
{
    return !(a == b);
}

[[nodiscard]] inline std::string editorDocumentIdKey(const FEditorDocumentId& id)
{
    char prefix = 's';
    switch (id.kind) {
    case EEditorDocumentKind::Scene: {
        prefix = 's';
        break;
    }
    case EEditorDocumentKind::UI: {
        prefix = 'u';
        break;
    }
    case EEditorDocumentKind::Material: {
        prefix = 'm';
        break;
    }
    case EEditorDocumentKind::Script: {
        prefix = 'c';
        break;
    }
    }
    std::string key;
    key.push_back(prefix);
    key.push_back(':');
    key += id.key;
    return key;
}

[[nodiscard]] inline FEditorDocumentId makeEditorSceneDocumentId(std::string_view path)
{
    FEditorDocumentId id;
    id.kind = EEditorDocumentKind::Scene;
    id.key  = path.empty() ? std::string("untitled") : std::string(path);
    return id;
}

[[nodiscard]] inline FEditorDocumentId makeEditorUIDocumentId(std::string_view key)
{
    FEditorDocumentId id;
    id.kind = EEditorDocumentKind::UI;
    id.key  = std::string(key);
    return id;
}

/// How a document answers close. Tab detach policy is separate
/// (`EEditorTabDetachPolicy` on the Level Editor tab).
enum class EEditorDocumentClosePolicy : uint8_t
{
    Discard,       ///< requestClose always succeeds; dirty is dropped
    RejectIfDirty, ///< requestClose fails while dirty
    Locked,        ///< cannot close except Force (process teardown)
};

enum class EEditorDocumentCloseMode : uint8_t
{
    Request, ///< honor close policy
    Discard, ///< ignore dirty; still honor Locked
    Force,   ///< ignore dirty and Locked (module detach)
};

enum class EEditorDocumentCloseResult : uint8_t
{
    Closed,
    RejectedDirty,
    RejectedLocked,
    Missing,
};

/// PreviewTarget claim, not a Camera and not a present surface. At most one
/// session per document kind may own the preview host.
enum class EEditorPreviewOwner : uint8_t
{
    None,
    Document,
};

/// Document-scoped dirty / undo / close / preview claim. Selection stays on
/// `EditorRootSession` (per window). Do not put this table on EditorSurface
/// or EditorWindowSession.
struct EditorDocumentSession
{
private:
    FEditorDocumentId          _id;
    EEditorDocumentClosePolicy _closePolicy = EEditorDocumentClosePolicy::RejectIfDirty;
    std::shared_ptr<UndoStack> _undo        = std::make_shared<UndoStack>();
    int                        _bindCount   = 0;
    bool                       _dirty       = false;
    bool                       _previewClaimed = false;

public:
    EditorDocumentSession(FEditorDocumentId id, EEditorDocumentClosePolicy policy)
        : _id(std::move(id))
        , _closePolicy(policy)
    {
    }

    EditorDocumentSession(const EditorDocumentSession&)            = delete;
    EditorDocumentSession& operator=(const EditorDocumentSession&) = delete;

    [[nodiscard]] const FEditorDocumentId& id() const { return _id; }
    [[nodiscard]] EEditorDocumentClosePolicy closePolicy() const { return _closePolicy; }
    [[nodiscard]] bool dirty() const { return _dirty; }
    void markDirty() { _dirty = true; }
    void clearDirty() { _dirty = false; }

    [[nodiscard]] UndoStack& undo() { return *_undo; }
    [[nodiscard]] const UndoStack& undo() const { return *_undo; }

    [[nodiscard]] int bindCount() const { return _bindCount; }
    void addBind() { ++_bindCount; }
    void releaseBind()
    {
        if (_bindCount > 0) {
            --_bindCount;
        }
    }

    [[nodiscard]] bool ownsPreview() const { return _previewClaimed; }
    void claimPreview() { _previewClaimed = true; }
    void releasePreview() { _previewClaimed = false; }
};

/// App-owned singleton table. `open` of the same id returns the same session.
/// Owned by EditorModule, not Surface / WindowSession.
struct EditorDocumentRegistry
{
private:
    std::unordered_map<std::string, std::unique_ptr<EditorDocumentSession>> _sessions;
    uint64_t _untitledSerial = 0;

public:
    EditorDocumentSession* open(FEditorDocumentId id, EEditorDocumentClosePolicy policy);
    [[nodiscard]] EditorDocumentSession* find(const FEditorDocumentId& id);
    [[nodiscard]] const EditorDocumentSession* find(const FEditorDocumentId& id) const;

    EEditorDocumentCloseResult close(const FEditorDocumentId& id,
                                     EEditorDocumentCloseMode mode = EEditorDocumentCloseMode::Request);

    [[nodiscard]] bool claimPreview(const FEditorDocumentId& id);
    void releasePreview(const FEditorDocumentId& id);

    [[nodiscard]] std::string makeUntitledKey();
    [[nodiscard]] size_t size() const { return _sessions.size(); }
};

} // namespace ya
