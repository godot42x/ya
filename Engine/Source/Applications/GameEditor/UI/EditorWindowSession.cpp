#include "GameEditor/UI/EditorWindowSession.h"

#include "GameEditor/EditorLayer.h"

namespace ya
{

void EditorWindowSession::bind(EditorLayer& layer,
                               EditorTabSpawnerRegistry* spawners,
                               EditorDocumentRegistry* documents)
{
    if (documents) {
        bindSceneDocument(*documents, layer.getCurrentScenePath());
    }
    _documents = documents;
    _surface.bind(layer, spawners, &_levelRoot, _windowId, documents, roots());
}

void EditorWindowSession::bindSceneDocument(EditorDocumentRegistry& documents, std::string_view path)
{
    EditorDocumentSession* previous = _levelRoot.document();
    const FEditorDocumentId previousId = previous ? previous->id() : FEditorDocumentId{};
    EditorDocumentSession* next =
        documents.open(makeEditorSceneDocumentId(path), EEditorDocumentClosePolicy::Discard);
    _levelRoot.bindDocument(next);
    if (previousId.valid() && previousId != (next ? next->id() : FEditorDocumentId{})) {
        if (EditorDocumentSession* leftover = documents.find(previousId)) {
            if (leftover->bindCount() == 0) {
                (void)documents.close(previousId, EEditorDocumentCloseMode::Discard);
            }
        }
    }
}

void EditorWindowSession::tick(const FEditorSurfaceContext& context, float dt)
{
    _metrics = context.metrics;
    _surface.tick(context, dt);
}

} // namespace ya
