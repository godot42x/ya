#include "Core/Reflection/PropertyAccessor.h"
#include "Core/Reflection/Reflection.h"

#include <gtest/gtest.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ya
{

struct NestedParams
{
    YA_REFLECT_BEGIN(NestedParams)
    YA_REFLECT_FIELD(scale)
    YA_REFLECT_FIELD(count)
    YA_REFLECT_END()

    glm::vec2 scale{1.0f, 2.0f};
    int       count = 3;
};

struct NestedOwner
{
    YA_REFLECT_BEGIN(NestedOwner)
    YA_REFLECT_FIELD(enabled)
    YA_REFLECT_FIELD(params)
    YA_REFLECT_END()

    bool         enabled = true;
    NestedParams params;
};

TEST(PropertyAccessorTest, ReadsWritesAndValidatesSingleInstance)
{
    NestedOwner owner;
    const Class* cls = ClassRegistry::instance().getClass(type_index_v<NestedOwner>);
    ASSERT_NE(cls, nullptr);
    const Property& enabled = cls->properties.at("enabled");

    bool value = false;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetBool(enabled, &owner, value));
    EXPECT_TRUE(value);
    EXPECT_TRUE(reflection::PropertyAccessor::setBool(enabled, &owner, false));
    EXPECT_FALSE(owner.enabled);
    EXPECT_FALSE(reflection::PropertyAccessor::setBool(enabled, &owner, false));
}

TEST(PropertyAccessorTest, CollectLeavesFlattensNestedCompositeFields)
{
    NestedOwner owner;
    std::vector<reflection::PropertyAccessor::FLeaf> leaves;
    reflection::PropertyAccessor::collectLeaves(type_index_v<NestedOwner>, {&owner}, leaves);

    ASSERT_EQ(leaves.size(), 3u);
    EXPECT_EQ(leaves[0].path, "enabled");
    EXPECT_EQ(leaves[1].path, "params.scale");
    EXPECT_EQ(leaves[2].path, "params.count");
    ASSERT_NE(leaves[1].property, nullptr);
    EXPECT_EQ(leaves[1].ownerType, type_index_v<NestedParams>);
    ASSERT_EQ(leaves[1].ownerInstances.size(), 1u);
    EXPECT_EQ(leaves[1].ownerInstances.front(), static_cast<void*>(&owner.params));

    glm::vec2 scale{};
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetVec2(*leaves[1].property, leaves[1].ownerInstances.front(), scale));
    EXPECT_EQ(scale, glm::vec2(1.0f, 2.0f));
    EXPECT_TRUE(reflection::PropertyAccessor::setVec2(*leaves[1].property, leaves[1].ownerInstances.front(), {4.0f, 5.0f}));
    EXPECT_EQ(owner.params.scale, glm::vec2(4.0f, 5.0f));

    int64_t count = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(*leaves[2].property, leaves[2].ownerInstances.front(), count));
    EXPECT_EQ(count, 3);
}

TEST(PropertyAccessorTest, EqualsDetectsScalarAndVectorDifferences)
{
    NestedOwner first;
    NestedOwner second;
    second.params.scale.x = 9.0f;
    second.params.count = 8;

    std::vector<reflection::PropertyAccessor::FLeaf> leaves;
    reflection::PropertyAccessor::collectLeaves(type_index_v<NestedOwner>, {&first}, leaves);
    const Property* scale = nullptr;
    const Property* count = nullptr;
    for (const auto& leaf : leaves) {
        if (leaf.path == "params.scale") {
            scale = leaf.property;
        }
        if (leaf.path == "params.count") {
            count = leaf.property;
        }
    }
    ASSERT_NE(scale, nullptr);
    ASSERT_NE(count, nullptr);

    const void* firstScale = reflection::PropertyAccessor::address(*scale, &first.params);
    const void* secondScale = reflection::PropertyAccessor::address(*scale, &second.params);
    EXPECT_FALSE(reflection::PropertyAccessor::equals(*scale, firstScale, secondScale));
    EXPECT_FALSE(reflection::PropertyAccessor::equalsVecAxis(*scale, firstScale, secondScale, 0, 2));
    EXPECT_TRUE(reflection::PropertyAccessor::equalsVecAxis(*scale, firstScale, secondScale, 1, 2));

    const void* firstCount = reflection::PropertyAccessor::address(*count, &first.params);
    const void* secondCount = reflection::PropertyAccessor::address(*count, &second.params);
    EXPECT_FALSE(reflection::PropertyAccessor::equals(*count, firstCount, secondCount));
}

} // namespace ya
