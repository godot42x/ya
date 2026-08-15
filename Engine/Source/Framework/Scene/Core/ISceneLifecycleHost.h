#pragma once

// ============================================================================
// Scene registration seam (scene-core).
//
// Scene (data) must not reach Host/App services. Lifecycle registration is
// injected through this interface: SceneManager (scene-runtime) implements it
// and the Host binds it once at startup via Scene::setLifecycleHost().
// ============================================================================

namespace ya
{

struct Scene;

struct YA_SCENE_CORE_API ISceneLifecycleHost
{
    virtual ~ISceneLifecycleHost() = default;

    virtual void registerScenePointer(const Scene* scene)   = 0;
    virtual void unregisterScenePointer(const Scene* scene) = 0;
    virtual bool isSceneValid(const Scene* scene) const     = 0;
};

} // namespace ya
