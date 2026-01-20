/**
 * @file ContainerReflectionExample.cpp
 * @brief 容器反射完整示例
 * 
 * 展示如何使用统一的反射系统来处理容器
 */

#include "Core/Reflection/Reflection.h"
#include "Editor/ContainerPropertyRenderer.h"
#include <gtest/gtest.h>
#include <iostream>

using namespace ya;
using namespace ya::reflection;

// ============================================================================
// 示例：游戏角色背包系统
// ============================================================================

struct Item
{
    YA_REFLECT_BEGIN(Item)
    YA_REFLECT_FIELD(id)
    YA_REFLECT_FIELD(name)
    YA_REFLECT_FIELD(count)
    YA_REFLECT_END()

    int         id    = 0;
    std::string name  = "";
    int         count = 0;
};

struct PlayerInventory
{
    YA_REFLECT_BEGIN(PlayerInventory)
    YA_REFLECT_FIELD(gold)
    YA_REFLECT_FIELD(items)        // std::vector<Item> - 自动检测为容器！
    YA_REFLECT_FIELD(equipSlots)   // std::map<std::string, int>
    YA_REFLECT_FIELD(unlocks)      // std::set<int>
    YA_REFLECT_END()

    int                          gold = 100;
    std::vector<Item>            items;
    std::map<std::string, int>   equipSlots;
    std::set<int>                unlocks;
};

// ============================================================================
// 测试函数
// ============================================================================

class ContainerReflectionTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试1: 容器迭代
TEST_F(ContainerReflectionTest, ContainerIteration)
{
    PlayerInventory inventory;
    inventory.items = {
        {1, "Sword", 1},
        {2, "Potion", 5},
        {3, "Shield", 1}};

    inventory.equipSlots = {
        {"Weapon", 1},
        {"Armor", 3}};

    auto &registry = ClassRegistry::instance();
    auto *cls      = registry.getClass(ya::type_index_v<PlayerInventory>);

    ASSERT_NE(cls, nullptr);

    auto *itemsProp = cls->getProperty("items");
    ASSERT_NE(itemsProp, nullptr);

    size_t itemCount = 0;
    void *containerPtr = itemsProp->addressGetterMutable(&inventory);

    PropertyContainerHelper::iterateContainer(
        *itemsProp,
        containerPtr,
        [&itemCount](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
            auto *item = static_cast<Item *>(elementPtr);
            itemCount++;
        });

    EXPECT_EQ(itemCount, 3);

    auto *slotsProp = cls->getProperty("equipSlots");
    ASSERT_NE(slotsProp, nullptr);

    size_t slotCount = 0;
    containerPtr = slotsProp->addressGetterMutable(&inventory);

    PropertyContainerHelper::iterateMapContainer(
        *slotsProp,
        containerPtr,
        [&slotCount](void *keyPtr, uint32_t keyTypeIndex, void *valuePtr, uint32_t valueTypeIndex) {
            slotCount++;
        });

    EXPECT_EQ(slotCount, 2);
}

// 测试2: 容器操作
TEST_F(ContainerReflectionTest, ContainerManipulation)
{
    PlayerInventory inventory;

    auto &registry = ClassRegistry::instance();
    auto *cls      = registry.getClass(ya::type_index_v<PlayerInventory>);
    auto *itemsProp = cls->getProperty("items");

    ASSERT_NE(itemsProp, nullptr);

    void *containerPtr = itemsProp->addressGetterMutable(&inventory);
    auto *accessor     = PropertyContainerHelper::getContainerAccessor(*itemsProp);

    ASSERT_NE(accessor, nullptr);

    // 添加元素
    Item newItem{99, "Magic Scroll", 1};
    accessor->addElement(containerPtr, &newItem);

    EXPECT_EQ(accessor->getSize(containerPtr), 1);

    // 清空
    accessor->clear(containerPtr);
    EXPECT_EQ(accessor->getSize(containerPtr), 0);
}
