#include "reflects-core/lib.h"
#include <iostream>



// ============================================================================
// 测试2: 多重继承（非虚）
// ============================================================================
struct Base1
{
    int value1 = 10;
};

struct Base2
{
    float value2 = 3.14f;
};

struct MultiDerived : public Base1, public Base2
{
    double value3 = 2.718;
};

// ============================================================================
// 测试3: 普通单继承 + 虚函数（验证 vtable 不影响偏移）
// ============================================================================
struct VirtualBase2
{
    virtual ~VirtualBase2() = default;
    virtual void virtualMethod() {}
    int          vbValue2 = 500;
};

struct VirtualDerived : public VirtualBase2
{
    int vdValue = 600;

    void virtualMethod() override {}
};


#include <gtest/gtest.h>


TEST(ReflectsCore, MultipleInheritance)
{
    Register<Base1>("Base1").property("value1", &Base1::value1);
    Register<Base2>("Base2").property("value2", &Base2::value2);
    Register<MultiDerived>("MultiDerived")
        .parentClass<Base1>()
        .parentClass<Base2>()
        .property("value3", &MultiDerived::value3);

    MultiDerived obj;
    obj.value1 = 42;
    obj.value2 = 1.414f;
    obj.value3 = 1.732;

    Class *cls = ClassRegistry::instance().getClass("MultiDerived");
    ASSERT_NE(cls, nullptr);

    std::vector<std::string> names, types;
    std::vector<std::string> values;
    cls->visitAllProperties(&obj, [&](const std::string &name, const Property &prop, void *ptr) {
        names.push_back(name);
        if (prop.typeIndex == refl::type_index_v<int>) {
            int value = *static_cast<int *>(prop.addressGetterMutable(ptr));
            types.push_back("int");
            values.push_back(std::to_string(value));
        }
        else if (prop.typeIndex == refl::type_index_v<float>) {
            float value = *static_cast<float *>(prop.addressGetterMutable(ptr));
            types.push_back("float");
            values.push_back(std::to_string(value));
        }
        else if (prop.typeIndex == refl::type_index_v<double>) {
            double value = *static_cast<double *>(prop.addressGetterMutable(ptr));
            types.push_back("double");
            values.push_back(std::to_string(value));
        }
    });
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "value1");
    EXPECT_EQ(names[1], "value2");
    EXPECT_EQ(names[2], "value3");
    EXPECT_EQ(types[0], "int");
    EXPECT_EQ(types[1], "float");
    EXPECT_EQ(types[2], "double");
    EXPECT_EQ(values[0], "42");
    EXPECT_NEAR(std::stof(values[1]), 1.414f, 1e-5f);
    EXPECT_NEAR(std::stod(values[2]), 1.732, 1e-8);

    // 非递归
    names.clear();
    types.clear();
    values.clear();
    cls->visitAllProperties(&obj, [&](const std::string &name, const Property &prop, void *ptr) {
        names.push_back(name);
        if (prop.typeIndex == refl::type_index_v<double>) {
            double value = *static_cast<double*>(prop.addressGetterMutable(ptr));
            types.push_back("double");
            values.push_back(std::to_string(value));
        } }, false);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "value3");
    EXPECT_EQ(types[0], "double");
    EXPECT_NEAR(std::stod(values[0]), 1.732, 1e-8);
}


TEST(ReflectsCore, VirtualFunctionBase)
{
    Register<VirtualBase2>("VirtualBase2").property("vbValue2", &VirtualBase2::vbValue2);
    Register<VirtualDerived>("VirtualDerived").parentClass<VirtualBase2>().property("vdValue", &VirtualDerived::vdValue);

    VirtualDerived obj;
    obj.vbValue2 = 777;
    obj.vdValue  = 888;

    Class *cls = ClassRegistry::instance().getClass("VirtualDerived");
    ASSERT_NE(cls, nullptr);

    std::vector<std::string> names, types, values;
    cls->visitAllProperties(&obj, [&](const std::string &name, const Property &prop, void *ptr) {
        names.push_back(name);
        if (prop.typeIndex == refl::type_index_v<int>) {
            int value = *static_cast<int *>(prop.addressGetterMutable(ptr));
            types.push_back("int");
            values.push_back(std::to_string(value));
        }
    });
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "vbValue2");
    EXPECT_EQ(names[1], "vdValue");
    EXPECT_EQ(types[0], "int");
    EXPECT_EQ(types[1], "int");
    EXPECT_EQ(values[0], "777");
    EXPECT_EQ(values[1], "888");
}


