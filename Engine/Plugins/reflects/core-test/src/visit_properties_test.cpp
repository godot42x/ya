
#include "reflects-core/lib.h"
#include <gtest/gtest.h>


struct Base
{
    int baseValue = 10;
};

struct Derived : public Base
{
    float derivedValue = 3.14f;
};


TEST(ReflectsCore, VisitAllPropertiesRecursiveAndNonRecursive)
{
    Register<Base>("Base").property("baseValue", &Base::baseValue);
    Register<Derived>("Derived").parentClass<Base>().property("derivedValue", &Derived::derivedValue);

    Derived obj;
    obj.baseValue    = 42;
    obj.derivedValue = 2.718f;

    Class *derivedClass = ClassRegistry::instance().getClass("Derived");
    ASSERT_NE(derivedClass, nullptr);

    // 递归访问
    std::vector<std::string> names;
    std::vector<std::string> types;
    std::vector<std::string> values;
    derivedClass->visitAllProperties(&obj, [&](const std::string &name, const Property &prop, void *ptr) {
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
    });
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "baseValue");
    EXPECT_EQ(names[1], "derivedValue");
    EXPECT_EQ(types[0], "int");
    EXPECT_EQ(types[1], "float");
    EXPECT_EQ(values[0], "42");
    EXPECT_NEAR(std::stof(values[1]), 2.718f, 1e-5f);

    // 非递归访问
    names.clear();
    types.clear();
    values.clear();
    derivedClass->visitAllProperties(&obj, [&](const std::string &name, const Property &prop, void *ptr) {
        names.push_back(name);
        if (prop.typeIndex == refl::type_index_v<int>) {
            int value = *static_cast<int *>(prop.addressGetterMutable(ptr));
            types.push_back("int");
            values.push_back(std::to_string(value));
        } else if (prop.typeIndex == refl::type_index_v<float>) {
            float value = *static_cast<float *>(prop.addressGetterMutable(ptr));
            types.push_back("float");
            values.push_back(std::to_string(value));
        } }, false);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "derivedValue");
    EXPECT_EQ(types[0], "float");
    EXPECT_NEAR(std::stof(values[0]), 2.718f, 1e-5f);
}

// gtest main
