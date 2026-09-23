#include "Resource/AssetManager.h"
#include "Resource/Mesh.h"

#include <gtest/gtest.h>

namespace ya
{

// Resource-runtime closure guard: this target links ONLY the resource line
// (foundation + rhi + backend + resource core/loader/runtime). If resource
// code ever reaches ECS/Scene/Render3D/Host again, this target fails to
// link. The tests below are deliberately trivial: the point is the link
// closure, not runtime behavior (AssetManager state needs a live app).
TEST(ResourceRuntimeClosureTest, PublicTypesAreConsumable)
{
    // Normalization is a pure function; safe without app initialization.
    EXPECT_EQ(AssetManager::normalizeAssetPath("Engine:Content/A.png"), "Engine:Content/A.png");
    EXPECT_EQ(AssetManager::normalizeAssetPath("Engine\\Content\\A.png"), "Engine:Content/A.png");
}

TEST(ResourceRuntimeClosureTest, MeshTypeIsVisible)
{
    // Mesh (GPU mesh runtime type) must be directly consumable from the
    // resource-runtime public surface.
    static_assert(std::is_class_v<Mesh>, "Mesh must be a class type");
}

} // namespace ya
