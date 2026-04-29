#include "Runtime/App/Render/AnimationSystem.h"

#include "ECS/Component/SkeletonComponent.h"
#include "Render/SkeletonAnimationSampler.h"
#include "Runtime/App/App.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

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
    if (!app || !app->getSceneManager()) {
        return;
    }

    Scene* scene = app->getSceneManager()->getActiveScene();
    if (!scene) {
        return;
    }

    auto view = scene->getRegistry().view<SkeletonComponent>();
    view.each([deltaTime](SkeletonComponent& skeletonComp) {
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

        skeletonComp._pose = SkeletonAnimationSampler::samplePose(*skeletonComp.getSkeleton(),
                                                                  *clip,
                                                                  skeletonComp._time,
                                                                  skeletonComp._loop);
        skeletonComp.markPoseClean();
    });
}

} // namespace ya
