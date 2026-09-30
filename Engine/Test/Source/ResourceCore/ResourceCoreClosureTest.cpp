#include "Resource/Core/Meta/AssetMeta.h"

#include <gtest/gtest.h>

namespace ya
{

// Resource-core closure guard: this target links ONLY resource core plus the
// foundation. If resource-core code ever reaches the RHI, the resource
// runtime or anything above them, this target fails to link.
TEST(ResourceCoreClosureTest, AssetMetaIsConsumable)
{
    AssetMeta first;
    first.type = "texture";
    AssetMeta second = first;
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.propertiesHash(), second.propertiesHash());
}

} // namespace ya
