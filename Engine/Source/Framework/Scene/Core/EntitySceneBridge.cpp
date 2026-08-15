#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"

namespace ya::detail
{

namespace
{

void sceneEntityRename(Entity& entity, const std::string& newName)
{
    if (Scene* scene = entity.getScene()) {
        if (auto* node = scene->getNodeByEntity(&entity)) {
            node->setName(newName);
        }
    }
}

bool sceneEntityIsValid(const Entity& entity)
{
    const Scene* scene = entity.getScene();
    return scene != nullptr && scene->isValid() && scene->isValidEntity(&entity);
}

// Registered when ya-scene-core is loaded; ecs-core's default bridge stays a
// no-op in builds without the scene module (e.g. the GUI-only profile).
const bool g_sceneBridgeRegistered = (setEntitySceneBridge(&sceneEntityRename, &sceneEntityIsValid), true);

} // namespace

} // namespace ya::detail
