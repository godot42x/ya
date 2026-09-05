#include "Core/Reflection/PropertyAccessor.h"
#include "GameEditor/Inspector/PropertyGraphBuilder.h"
#include "Core/Reflection/Reflection.h"

#include <array>
#include <gtest/gtest.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <map>
#include <vector>

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
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(enabled, &owner, value));
    EXPECT_TRUE(value);
    EXPECT_TRUE(reflection::PropertyAccessor::set(enabled, &owner, false));
    EXPECT_FALSE(owner.enabled);
    EXPECT_FALSE(reflection::PropertyAccessor::set(enabled, &owner, false));
}

TEST(PropertyAccessorTest, TypedMutationResultPreservesFailureReason)
{
    NestedOwner owner;
    const Class* cls = ClassRegistry::instance().getClass(type_index_v<NestedOwner>);
    ASSERT_NE(cls, nullptr);
    const reflection::FPropertySlot enabled = reflection::FPropertySlot::field(cls->properties.at("enabled"));
    const auto unchanged = reflection::PropertyAccessor::setResult(enabled, &owner, true);
    EXPECT_EQ(unchanged.status, reflection::EPropertyMutationStatus::Unchanged);
    const auto mismatch = reflection::PropertyAccessor::setResult(enabled, &owner, 1.0f);
    EXPECT_EQ(mismatch.status, reflection::EPropertyMutationStatus::TypeMismatch);
}

TEST(PropertyAccessorTest, CollectLeavesFlattensNestedCompositeFields)
{
    NestedOwner owner;
    std::vector<PropertyGraphBuilder::FLeaf> leaves;
    PropertyGraphBuilder::collectLeaves(type_index_v<NestedOwner>, {&owner}, leaves);

    ASSERT_EQ(leaves.size(), 3u);
    EXPECT_EQ(leaves[0].path, "enabled");
    EXPECT_EQ(leaves[1].path, "params.scale");
    EXPECT_EQ(leaves[2].path, "params.count");
    ASSERT_NE(leaves[1].slot.property, nullptr);
    EXPECT_EQ(leaves[1].ownerType, type_index_v<NestedParams>);
    ASSERT_EQ(leaves[1].ownerInstances.size(), 1u);
    EXPECT_EQ(leaves[1].ownerInstances.front(), static_cast<void*>(&owner.params));

    glm::vec2 scale{};
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(leaves[1].slot, leaves[1].ownerInstances.front(), scale));
    EXPECT_EQ(scale, glm::vec2(1.0f, 2.0f));
    EXPECT_TRUE(reflection::PropertyAccessor::set(leaves[1].slot, leaves[1].ownerInstances.front(), glm::vec2{4.0f, 5.0f}));
    EXPECT_EQ(owner.params.scale, glm::vec2(4.0f, 5.0f));

    int64_t count = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(leaves[2].slot, leaves[2].ownerInstances.front(), count));
    EXPECT_EQ(count, 3);
}

TEST(PropertyAccessorTest, EqualsDetectsScalarAndVectorDifferences)
{
    NestedOwner first;
    NestedOwner second;
    second.params.scale.x = 9.0f;
    second.params.count = 8;

    std::vector<PropertyGraphBuilder::FLeaf> leaves;
    PropertyGraphBuilder::collectLeaves(type_index_v<NestedOwner>, {&first}, leaves);
    reflection::FPropertySlot scale;
    reflection::FPropertySlot count;
    for (const auto& leaf : leaves) {
        if (leaf.path == "params.scale") {
            scale = leaf.slot;
        }
        if (leaf.path == "params.count") {
            count = leaf.slot;
        }
    }
    ASSERT_TRUE(scale.isValid());
    ASSERT_TRUE(count.isValid());

    const void* firstScale = reflection::PropertyAccessor::address(scale, &first.params);
    const void* secondScale = reflection::PropertyAccessor::address(scale, &second.params);
    EXPECT_FALSE(reflection::PropertyAccessor::equals(scale, firstScale, secondScale));
    EXPECT_FALSE(reflection::PropertyAccessor::equalsVecAxis(scale, firstScale, secondScale, 0, 2));
    EXPECT_TRUE(reflection::PropertyAccessor::equalsVecAxis(scale, firstScale, secondScale, 1, 2));

    const void* firstCount = reflection::PropertyAccessor::address(count, &first.params);
    const void* secondCount = reflection::PropertyAccessor::address(count, &second.params);
    EXPECT_FALSE(reflection::PropertyAccessor::equals(count, firstCount, secondCount));
}

struct SequenceOwner
{
    YA_REFLECT_BEGIN(SequenceOwner)
    YA_REFLECT_FIELD(files)
    YA_REFLECT_FIELD(weights)
    YA_REFLECT_END()

    std::array<std::string, 2> files{"posx.hdr", "negx.hdr"};
    std::vector<float>         weights{0.25f, 0.75f};
};

TEST(PropertyAccessorTest, CollectLeavesExpandsSequenceOfLeafElements)
{
    SequenceOwner owner;
    std::vector<PropertyGraphBuilder::FLeaf> leaves;
    PropertyGraphBuilder::collectLeaves(type_index_v<SequenceOwner>, {&owner}, leaves);

    ASSERT_EQ(leaves.size(), 5u);
    EXPECT_EQ(leaves[0].path, "files[0]");
    EXPECT_EQ(leaves[1].path, "files[1]");
    EXPECT_EQ(leaves[2].path, "weights");
    EXPECT_EQ(leaves[2].role, PropertyGraphBuilder::ELeafRole::Sequence);
    EXPECT_EQ(leaves[3].path, "weights[0]");
    EXPECT_EQ(leaves[4].path, "weights[1]");
    EXPECT_EQ(leaves[0].slot.elementIndex, 0);
    EXPECT_EQ(leaves[1].slot.elementIndex, 1);
    EXPECT_EQ(reflection::PropertyAccessor::valueType(leaves[0].slot),
              type_index_v<std::string>);
    EXPECT_EQ(reflection::PropertyAccessor::valueType(leaves[3].slot),
              type_index_v<float>);

    std::string face;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(leaves[0].slot, &owner, face));
    EXPECT_EQ(face, "posx.hdr");
    EXPECT_TRUE(reflection::PropertyAccessor::set(leaves[0].slot, &owner, std::string{"front.hdr"}));
    EXPECT_EQ(owner.files[0], "front.hdr");
    EXPECT_EQ(owner.files[1], "negx.hdr");

    float weight = 0.0f;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGet(leaves[3].slot, &owner, weight));
    EXPECT_FLOAT_EQ(weight, 0.25f);
    EXPECT_TRUE(reflection::PropertyAccessor::set(leaves[3].slot, &owner, 0.5f));
    EXPECT_FLOAT_EQ(owner.weights[0], 0.5f);
    EXPECT_FLOAT_EQ(owner.weights[1], 0.75f);

    SequenceOwner other = owner;
    other.files[0] = "other.hdr";
    const void* first = reflection::PropertyAccessor::address(leaves[0].slot, &owner);
    const void* second = reflection::PropertyAccessor::address(leaves[0].slot, &other);
    EXPECT_FALSE(reflection::PropertyAccessor::equals(leaves[0].slot, first, second));
}

struct MapOwner
{
    YA_REFLECT_BEGIN(MapOwner)
    YA_REFLECT_FIELD(slots)
    YA_REFLECT_END()

    std::map<std::string, int> slots{{"sword", 2}, {"shield", 1}};
};

TEST(PropertyAccessorTest, CollectLeavesExpandsMapOfLeafValuesAndMutates)
{
    MapOwner owner;
    std::vector<PropertyGraphBuilder::FLeaf> leaves;
    PropertyGraphBuilder::collectLeaves(type_index_v<MapOwner>, {&owner}, leaves);

    ASSERT_EQ(leaves.size(), 3u);
    EXPECT_EQ(leaves[0].path, "slots");
    EXPECT_EQ(leaves[0].role, PropertyGraphBuilder::ELeafRole::Map);
    EXPECT_EQ(leaves[1].path, "slots[\"shield\"]");
    EXPECT_EQ(leaves[2].path, "slots[\"sword\"]");

    int64_t sword = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(leaves[2].slot, &owner, sword));
    EXPECT_EQ(sword, 2);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(leaves[2].slot, &owner, 5));
    EXPECT_EQ(owner.slots["sword"], 5);

    EXPECT_TRUE(reflection::PropertyAccessor::insertMapKey(*leaves[0].slot.property, &owner, "bow"));
    EXPECT_EQ(owner.slots.count("bow"), 1u);
    EXPECT_TRUE(reflection::PropertyAccessor::removeMapKey(*leaves[0].slot.property, &owner, "shield"));
    EXPECT_EQ(owner.slots.count("shield"), 0u);
}

TEST(PropertyAccessorTest, EmptyStringMapKeyIsRepresentedAsMapSlot)
{
    MapOwner owner;
    owner.slots.emplace("", 7);

    const Class* cls = ClassRegistry::instance().getClass(type_index_v<MapOwner>);
    ASSERT_NE(cls, nullptr);
    const Property& slots = cls->properties.at("slots");
    const reflection::FPropertySlot slot = reflection::FPropertySlot::at(slots, std::string{});

    EXPECT_TRUE(slot.isMapValue());
    EXPECT_FALSE(slot.isField());

    int64_t value = 0;
    ASSERT_TRUE(reflection::PropertyAccessor::tryGetInteger(slot, &owner, value));
    EXPECT_EQ(value, 7);
    EXPECT_TRUE(reflection::PropertyAccessor::setInteger(slot, &owner, 9));
    EXPECT_EQ(owner.slots.at(""), 9);
    EXPECT_TRUE(reflection::PropertyAccessor::removeMapKey(slots, &owner, ""));
    EXPECT_EQ(owner.slots.count(""), 0u);
}

TEST(PropertyAccessorTest, DynamicSequenceAppendRemoveAndClear)
{
    SequenceOwner owner;
    const Class* cls = ClassRegistry::instance().getClass(type_index_v<SequenceOwner>);
    ASSERT_NE(cls, nullptr);
    const Property& weights = cls->properties.at("weights");

    EXPECT_TRUE(reflection::PropertyAccessor::isDynamicSequence(weights));
    EXPECT_FALSE(reflection::PropertyAccessor::isDynamicSequence(cls->properties.at("files")));
    EXPECT_EQ(reflection::PropertyAccessor::containerSize(weights, &owner), 2u);
    EXPECT_TRUE(reflection::PropertyAccessor::appendEmpty(weights, &owner));
    EXPECT_EQ(owner.weights.size(), 3u);
    EXPECT_TRUE(reflection::PropertyAccessor::removeAt(weights, &owner, 1));
    EXPECT_EQ(owner.weights.size(), 2u);
    EXPECT_FLOAT_EQ(owner.weights[0], 0.25f);
    EXPECT_TRUE(reflection::PropertyAccessor::clearContainer(weights, &owner));
    EXPECT_TRUE(owner.weights.empty());
    EXPECT_TRUE(reflection::PropertyAccessor::insertEmptyAt(weights, &owner, 0));
    EXPECT_EQ(owner.weights.size(), 1u);
}

} // namespace ya
