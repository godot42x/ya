#include "GameEditor/UI/Shell/EditorDocumentSession.h"

namespace ya
{

EditorDocumentSession* EditorDocumentRegistry::open(FEditorDocumentId id, EEditorDocumentClosePolicy policy)
{
    if (!id.valid()) {
        return nullptr;
    }
    const std::string key = editorDocumentIdKey(id);
    if (auto it = _sessions.find(key); it != _sessions.end()) {
        return it->second.get();
    }
    auto session = std::make_unique<EditorDocumentSession>(std::move(id), policy);
    EditorDocumentSession* raw = session.get();
    _sessions.emplace(key, std::move(session));
    return raw;
}

EditorDocumentSession* EditorDocumentRegistry::find(const FEditorDocumentId& id)
{
    if (!id.valid()) {
        return nullptr;
    }
    const auto it = _sessions.find(editorDocumentIdKey(id));
    return it == _sessions.end() ? nullptr : it->second.get();
}

const EditorDocumentSession* EditorDocumentRegistry::find(const FEditorDocumentId& id) const
{
    if (!id.valid()) {
        return nullptr;
    }
    const auto it = _sessions.find(editorDocumentIdKey(id));
    return it == _sessions.end() ? nullptr : it->second.get();
}

EEditorDocumentCloseResult EditorDocumentRegistry::close(const FEditorDocumentId& id,
                                                         EEditorDocumentCloseMode mode)
{
    if (!id.valid()) {
        return EEditorDocumentCloseResult::Missing;
    }
    const std::string key = editorDocumentIdKey(id);
    const auto        it  = _sessions.find(key);
    if (it == _sessions.end() || !it->second) {
        return EEditorDocumentCloseResult::Missing;
    }
    EditorDocumentSession& session = *it->second;
    if (session.closePolicy() == EEditorDocumentClosePolicy::Locked &&
        mode != EEditorDocumentCloseMode::Force) {
        return EEditorDocumentCloseResult::RejectedLocked;
    }
    if (mode == EEditorDocumentCloseMode::Request &&
        session.closePolicy() == EEditorDocumentClosePolicy::RejectIfDirty &&
        session.dirty()) {
        return EEditorDocumentCloseResult::RejectedDirty;
    }
    if (mode != EEditorDocumentCloseMode::Force && session.bindCount() > 0) {
        return EEditorDocumentCloseResult::RejectedLocked;
    }
    session.releasePreview();
    _sessions.erase(it);
    return EEditorDocumentCloseResult::Closed;
}

bool EditorDocumentRegistry::claimPreview(const FEditorDocumentId& id)
{
    EditorDocumentSession* session = find(id);
    if (!session) {
        return false;
    }
    if (session->ownsPreview()) {
        return true;
    }
    for (const auto& [_, other] : _sessions) {
        if (other && other->id().kind == id.kind && other->ownsPreview()) {
            return false;
        }
    }
    session->claimPreview();
    return true;
}

void EditorDocumentRegistry::releasePreview(const FEditorDocumentId& id)
{
    if (EditorDocumentSession* session = find(id)) {
        session->releasePreview();
    }
}

std::string EditorDocumentRegistry::makeUntitledKey()
{
    return "untitled-" + std::to_string(++_untitledSerial);
}

} // namespace ya
