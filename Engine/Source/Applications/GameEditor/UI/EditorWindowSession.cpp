#include "GameEditor/UI/EditorWindowSession.h"

namespace ya
{

void EditorWindowSession::tick(App& app, float dt)
{
    _surface.tick(app, dt);
}

void EditorWindowSession::tick(const FEditorSurfaceContext& context, float dt)
{
    _metrics = context.metrics;
    _surface.tick(context, dt);
}

} // namespace ya
