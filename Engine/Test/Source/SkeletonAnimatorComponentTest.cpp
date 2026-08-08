#include "ECS/Component/SkeletonAnimatorComponent.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(SkeletonAnimatorComponentTest, SetFromModelStoresRuntimeSkeletonReference)
{
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->name = "TestSkeleton";

    SkeletonAnimatorComponent component;
    component.setFromModel("Content/Models/Character.fbx", 2, 1, skeleton);

    EXPECT_EQ(component._sourceModelPath, "Content/Models/Character.fbx");
    EXPECT_EQ(component._meshIndex, 2u);
    EXPECT_EQ(component._skeletonIndex, 1u);
    EXPECT_TRUE(component.hasSkeleton());
    EXPECT_EQ(component.getSkeleton(), skeleton.get());
    EXPECT_TRUE(component.isPoseDirty());
}

TEST(SkeletonAnimatorComponentTest, ValidClipRequiresSkeletonAndClipIndex)
{
    SkeletonAnimatorComponent component;
    EXPECT_FALSE(component.hasValidClip());
    EXPECT_EQ(component.getClip(), nullptr);

    auto skeleton = std::make_shared<Skeleton>();
    skeleton->animations.push_back(SkeletonAnimationClip{.name = "Idle"});
    component.setFromModel("Content/Models/Character.fbx", 0, 0, skeleton);

    component._clipIndex = 0;
    EXPECT_TRUE(component.hasValidClip());
    ASSERT_NE(component.getClip(), nullptr);
    EXPECT_EQ(component.getClip()->name, "Idle");

    component._clipIndex = 1;
    EXPECT_FALSE(component.hasValidClip());
    EXPECT_EQ(component.getClip(), nullptr);
}

TEST(SkeletonAnimatorComponentTest, PoseDirtyFlagCanBeClearedAndInvalidated)
{
    SkeletonAnimatorComponent component;
    EXPECT_TRUE(component.isPoseDirty());

    component.markPoseClean();
    EXPECT_FALSE(component.isPoseDirty());

    component.invalidatePose();
    EXPECT_TRUE(component.isPoseDirty());
}

} // namespace ya
