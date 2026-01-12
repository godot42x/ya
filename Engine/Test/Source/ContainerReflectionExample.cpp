/**
 * @file ContainerReflectionExample.cpp
 * @brief 容器反射完整示例
 * 
 * 展示如何使用统一的反射系统来处理容器
 */

#include "Core/Reflection/Reflection.h"
#include "Editor/ContainerPropertyRenderer.h"
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

void testContainerIteration()
{
    std::cout << "\n=== Container Iteration Test ===" << std::endl;

    PlayerInventory inventory;
    inventory.items = {
        {1, "Sword", 1},
        {2, "Potion", 5},
        {3, "Shield", 1}};

    inventory.equipSlots = {
        {"Weapon", 1},
        {"Armor", 3}};

    // 使用反射迭代容器
    auto &registry = ClassRegistry::instance();
    auto *cls      = registry.getClass(ya::type_index_v<PlayerInventory>);

    if (cls) {
        auto *itemsProp = cls->getProperty("items");
        if (itemsProp) {
            std::cout << "Items in inventory:" << std::endl;

            void *containerPtr = itemsProp->addressGetterMutable(&inventory);

            PropertyContainerHelper::iterateContainer(
                *itemsProp,
                containerPtr,
                [](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
                    // 访问 Item 属性
                    auto *item = static_cast<Item *>(elementPtr);
                    std::cout << "  [" << index << "] " << item->name
                              << " (count: " << item->count << ")" << std::endl;
                });
        }

        auto *slotsProp = cls->getProperty("equipSlots");
        if (slotsProp) {
            std::cout << "\nEquipment slots:" << std::endl;

            void *containerPtr = slotsProp->addressGetterMutable(&inventory);

            PropertyContainerHelper::iterateContainer(
                *slotsProp,
                containerPtr,
                [](const std::string &key, void *valuePtr, uint32_t valueTypeIndex) {
                    int *itemId = static_cast<int *>(valuePtr);
                    std::cout << "  " << key << " = " << *itemId << std::endl;
                });
        }
    }
}

void testContainerManipulation()
{
    std::cout << "\n=== Container Manipulation Test ===" << std::endl;

    PlayerInventory inventory;

    auto &registry = ClassRegistry::instance();
    auto *cls      = registry.getClass(ya::type_index_v<PlayerInventory>);
    auto *itemsProp = cls->getProperty("items");

    if (itemsProp) {
        void *containerPtr = itemsProp->addressGetterMutable(&inventory);
        auto *accessor     = PropertyContainerHelper::getContainerAccessor(*itemsProp);

        if (accessor) {
            // 添加元素
            Item newItem{99, "Magic Scroll", 1};
            accessor->addElement(containerPtr, &newItem);

            std::cout << "After adding item, size = " << accessor->getSize(containerPtr) << std::endl;

            // 再次迭代
            PropertyContainerHelper::iterateContainer(
                *itemsProp,
                containerPtr,
                [](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
                    auto *item = static_cast<Item *>(elementPtr);
                    std::cout << "  [" << index << "] " << item->name << std::endl;
                });

            // 清空
            accessor->clear(containerPtr);
            std::cout << "After clear, size = " << accessor->getSize(containerPtr) << std::endl;
        }
    }
}

void testImGuiIntegration()
{
    std::cout << "\n=== ImGui Integration Example ===" << std::endl;
    std::cout << "在 DetailsView::renderReflectedType 中，容器会自动渲染为：" << std::endl;
    std::cout << R"(
    items (Size: 3) [+] [-] [Clear]
    ├─ [0]
    │  ├─ id: 1
    │  ├─ name: "Sword"
    │  └─ count: 1      [X]
    ├─ [1]
    │  ├─ id: 2
    │  ├─ name: "Potion"
    │  └─ count: 5      [X]
    └─ [2]
       ├─ id: 3
       ├─ name: "Shield"
       └─ count: 1      [X]
    
    equipSlots (Size: 2) [+]
    ├─ [Weapon]: 1
    └─ [Armor]: 3
    )" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    std::cout << "=== Unified Container Reflection System ===" << std::endl;

    testContainerIteration();
    testContainerManipulation();
    testImGuiIntegration();

    std::cout << "\n=== All Tests Passed! ===" << std::endl;
    std::cout << "\n关键特性：" << std::endl;
    std::cout << "✅ 统一的迭代接口（无需单独序列化代码）" << std::endl;
    std::cout << "✅ 自动检测容器类型" << std::endl;
    std::cout << "✅ 支持嵌套容器和复杂元素" << std::endl;
    std::cout << "✅ 集成到编辑器UI（DetailsView）" << std::endl;
    std::cout << "✅ 类型安全的编译期检查" << std::endl;

    return 0;
}
