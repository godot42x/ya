#include "Framework/Game/Render/Render3D/Systems/Animation/AnimationSystem.h"

#include "Framework/Game/Gameplay/ECS/Component/SkeletonAnimatorComponent.h"
#include "Framework/Game/Resource/SkeletonAnimationSampler.h"
#include "Product/Host/App.h"
#include "Framework/Game/Render/Render3D/RenderRuntime.h"
#include "Framework/Game/Render/Render3D/Scene.h"
#include "Framework/Game/Render/Render3D/SceneManager.h"

namespace ya
{
namespace
{
double resolveTicksPerSecond(const SkeletonAnimationClip& clip)
{
    return clip.ticksPerSecond > 0.0 ? clip.ticksPerSecond : 1.0;
}
} // namespace

void SkeletonAnimationSystem::onUpdate(float deltaTime)
{
    App* app = App::get();
    if (!app || !app->getSceneServices().getSceneManager()) {
        return;
    }

    // Skin poses are only consumed by the world pipeline's skinning palettes.
    // The editor 2D canvas mode disables the world scene graph, so sampling
    // every frame would be pure waste; the pose simply freezes while the
    // canvas is shown and resumes on the next enabled frame.
    auto* renderRuntime = app->getRenderServices().getRenderRuntime();
    if (renderRuntime && !renderRuntime->isWorldSceneRenderEnabled()) {
        return;
    }

    Scene* scene = app->getSceneServices().getActiveScene();
    if (!scene) {
        return;
    }

    auto view = scene->getRegistry().view<SkeletonAnimatorComponent>();
    view.each([deltaTime](SkeletonAnimatorComponent& skeletonComp) {
        const SkeletonAnimationClip* clip = skeletonComp.getClip();
        if (!skeletonComp.hasSkeleton() || !clip) {
            return;
        }

        if (skeletonComp._playing) {
            skeletonComp._time += static_cast<double>(deltaTime) * static_cast<double>(skeletonComp._speed) * resolveTicksPerSecond(*clip);
            skeletonComp.invalidatePose();
        }

        if (!skeletonComp.isPoseDirty()) {
            return;
        }

        SkeletonAnimationSampler::samplePose(*skeletonComp.getSkeleton(),
                                             *clip,
                                             skeletonComp._time,
                                             skeletonComp._loop,
                                             skeletonComp._pose);
        skeletonComp.markPoseClean();
    });
}

} // namespace ya
