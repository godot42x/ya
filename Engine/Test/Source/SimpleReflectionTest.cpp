// 简单测试新反射系统
#include "Core/Reflection/Reflection.h"
#include <cassert>
#include <iostream>


struct TestComponent
{
    int         value = 42;
    std::string name  = "test";

    YA_REFLECT_BEGIN(TestComponent)
    YA_REFLECT_FIELD(value, )
    YA_REFLECT_FIELD(name, )
    YA_REFLECT_END()
};

int main()
{
    std::cout << "=== 新反射系统测试 ===" << std::endl;

    // 测试1: 类型名称
    std::cout << "类型名称: " << TestComponent::getTypeName() << std::endl;
    assert(std::string(TestComponent::getTypeName()) == "TestComponent");

    // 测试2: 属性遍历
    int           propCount = 0;
    TestComponent p;
    p.__visit_properties([&](const char *name, auto &value) {
        std::cout << "  属性: " << name << std::endl;
        propCount++;
    });
    // TestComponent::__visit_properties([&](const char *name, auto ptr) {
    //     std::cout << "  属性: " << name << std::endl;
    //     propCount++;
    // });

    assert(propCount == 2); // value, name
    std::cout << "✅ 找到 " << propCount << " 个属性" << std::endl;

    std::cout << "\n=== 所有测试通过 ===" << std::endl;
    return 0;
}
