#pragma once

#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Skeleton.h"
#include "Render/SkeletonAnimationSampler.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace ya
{

struct SkeletonComponent : public IComponent
{
    YA_REFLECT_BEGIN(SkeletonComponent)
    YA_REFLECT_FIELD(_sourceModelPath)
    YA_REFLECT_FIELD(_meshIndex)
    YA_REFLECT_FIELD(_skeletonIndex)
    YA_REFLECT_FIELD(_clipIndex)
    YA_REFLECT_FIELD(_time)
    YA_REFLECT_FIELD(_speed)
    YA_REFLECT_FIELD(_loop)
    YA_REFLECT_FIELD(_playing)
    YA_REFLECT_END()

    std::string _sourceModelPath;
    uint32_t    _meshIndex     = 0;
    uint32_t    _skeletonIndex = std::numeric_limits<uint32_t>::max();

    std::shared_ptr<Skeleton> _skeleton;

    uint32_t _clipIndex = 0;
    double   _time      = 0.0;
    float    _speed     = 1.0f;
    bool     _loop      = true;
    bool     _playing   = true;

    SkeletonPose _pose;
    bool         _bPoseDirty = true;

    void setFromModel(const std::string&          modelPath,
                      uint32_t                    meshIndex,
                      uint32_t                    skeletonIndex,
                      std::shared_ptr<Skeleton>   skeleton)
    {
        _sourceModelPath = modelPath;
        _meshIndex       = meshIndex;
        _skeletonIndex   = skeletonIndex;
        _skeleton        = std::move(skeleton);
        _time            = 0.0;
        invalidatePose();
    }

    bool hasSkeleton() const { return _skeleton != nullptr; }
    const Skeleton* getSkeleton() const { return _skeleton.get(); }
    Skeleton* getSkeleton() { return _skeleton.get(); }

    bool hasValidClip() const
    {
        return _skeleton && _clipIndex < _skeleton->animations.size();
    }

    const SkeletonAnimationClip* getClip() const
    {
        return hasValidClip() ? &_skeleton->animations[_clipIndex] : nullptr;
    }

    const SkeletonPose& getPose() const { return _pose; }
    SkeletonPose&       getPoseMut() { return _pose; }

    void invalidatePose() { _bPoseDirty = true; }
    bool isPoseDirty() const { return _bPoseDirty; }
    void markPoseClean() { _bPoseDirty = false; }
};

} // namespace ya
