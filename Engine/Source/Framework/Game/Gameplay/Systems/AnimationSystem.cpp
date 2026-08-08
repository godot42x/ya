#include "Gameplay/Systems/AnimationSystem.h"

#include "Gameplay/Systems/SkeletonAnimatorComponent.h"
#include "Resource/SkeletonAnimationSampler.h"
#include "Scene/Core/Scene.h"

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
    if (!_sceneProvider) {
        return;
    }

    Scene* scene = _sceneProvider();
    if (!scene) {
        return;
    }

    // Explicit tick policy (bound by the Host): skin poses are only consumed
    // by the world pipeline's skinning palettes; when world rendering is
    // disabled (editor 2D canvas mode) sampling every frame would be pure
    // waste. The pose freezes while the policy says "don't tick".
    if (_tickPolicy && !_tickPolicy()) {
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

void SkeletonAnimationSystem::setSceneProvider(SceneProvider provider)
{
    _sceneProvider = std::move(provider);
}

void SkeletonAnimationSystem::setTickPolicy(TickPolicy policy)
{
    _tickPolicy = std::move(policy);
}

} // namespace ya
