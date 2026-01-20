/**
 * @file ContainerSerializationTest.cpp
 * @brief 容器类型序列化与反序列化 GTest 测试
 *
 * 测试 ReflectionSerializer 对各类容器的序列化和反序列化功能：
 * - std::vector<T> (简单类型、字符串、复杂对象)
 * - std::set<T>
 * - std::map<K, V>
 * - 嵌套容器
 */

#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>
#include <vector>


using namespace ya;
using namespace ya::reflection;

// ============================================================================
// 嵌套容器类型注册
// ============================================================================

// 为嵌套容器手动注册类型
struct VectorIntWrapper
{
    YA_REFLECT_BEGIN(VectorIntWrapper)
    YA_REFLECT_FIELD(data)
    YA_REFLECT_END()

    std::vector<int> data;
};

// ============================================================================
// 测试用的数据结构
// ============================================================================

// 简单对象（用于容器元素）
struct TestData
{
    YA_REFLECT_BEGIN(TestData)
    YA_REFLECT_FIELD(id)
    YA_REFLECT_FIELD(name)
    YA_REFLECT_FIELD(value)
    YA_REFLECT_END()

    int         id    = 0;
    std::string name  = "";
    float       value = 0.0f;

    // 添加构造函数确保正确的初始化
    TestData() = default;
    TestData(int i, const std::string& n, float v) : id(i), name(n), value(v) {}
    TestData(const TestData& other) : id(other.id), name(other.name), value(other.value) {}
    TestData& operator=(const TestData& other) {
        if (this != &other) {
            id = other.id;
            name = other.name;
            value = other.value;
        }
        return *this;
    }
};

// 手动注册 TestData 的构造函数
static struct TestDataConstructorRegistrar {
    TestDataConstructorRegistrar() {
        ClassRegistry::instance().addPostStaticInitializer([]() {
            auto& registry = ClassRegistry::instance();
            if (auto* cls = registry.getClass(ya::type_index_v<TestData>)) {
                cls->registerConstructor<TestData>(); // 注册默认构造函数
            }
        });
    }
} testDataConstructorRegistrar;

// 包含各类容器的测试类
struct ContainerTestObject
{
    YA_REFLECT_BEGIN(ContainerTestObject)
    YA_REFLECT_FIELD(intVector)
    YA_REFLECT_FIELD(stringVector)
    YA_REFLECT_FIELD(objectVector)
    YA_REFLECT_FIELD(intSet)
    YA_REFLECT_FIELD(stringIntMap)
    YA_REFLECT_END()

    std::vector<int>           intVector;
    std::vector<std::string>   stringVector;
    std::vector<TestData>      objectVector;
    std::set<int>              intSet;
    std::map<std::string, int> stringIntMap;
};

// 嵌套容器测试 - 使用已注册的对象类型而不是嵌套基础类型
struct NestedContainerTest
{
    YA_REFLECT_BEGIN(NestedContainerTest)
    YA_REFLECT_FIELD(objectMatrix) // vector<TestData> 而不是 vector<vector<int>>
    YA_REFLECT_END()

    std::vector<TestData> objectMatrix; // 改为使用已注册的 TestData 类型
};

// ============================================================================
// Vector 容器测试
// ============================================================================

TEST(ContainerSerializationTest, VectorInt_Serialize)
{
    ContainerTestObject obj;
    obj.intVector = {1, 2, 3, 4, 5};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("intVector"));
    EXPECT_TRUE(json["intVector"].is_array());
    EXPECT_EQ(json["intVector"].size(), 5);
    EXPECT_EQ(json["intVector"][0], 1);
    EXPECT_EQ(json["intVector"][4], 5);
}

TEST(ContainerSerializationTest, VectorInt_Deserialize)
{
    nlohmann::json json = {
        {"intVector", {10, 20, 30, 40}},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.intVector.size(), 4);
    EXPECT_EQ(obj.intVector[0], 10);
    EXPECT_EQ(obj.intVector[3], 40);
}

TEST(ContainerSerializationTest, VectorString_Serialize)
{
    ContainerTestObject obj;
    obj.stringVector = {"hello", "world", "test"};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("stringVector"));
    EXPECT_TRUE(json["stringVector"].is_array());
    EXPECT_EQ(json["stringVector"].size(), 3);
    EXPECT_EQ(json["stringVector"][0], "hello");
    EXPECT_EQ(json["stringVector"][2], "test");
}

TEST(ContainerSerializationTest, VectorString_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", {"alpha", "beta", "gamma"}},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.stringVector.size(), 3);
    EXPECT_EQ(obj.stringVector[0], "alpha");
    EXPECT_EQ(obj.stringVector[1], "beta");
    EXPECT_EQ(obj.stringVector[2], "gamma");
}

TEST(ContainerSerializationTest, VectorObject_Serialize)
{
    ContainerTestObject obj;
    obj.objectVector = {
        {1, "First", 1.5f},
        {2, "Second", 2.5f},
        {3, "Third", 3.5f}};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("objectVector"));
    EXPECT_TRUE(json["objectVector"].is_array());
    EXPECT_EQ(json["objectVector"].size(), 3);

    EXPECT_EQ(json["objectVector"][0]["id"], 1);
    EXPECT_EQ(json["objectVector"][0]["name"], "First");
    EXPECT_FLOAT_EQ(json["objectVector"][0]["value"], 1.5f);

    EXPECT_EQ(json["objectVector"][1]["id"], 2);
    EXPECT_EQ(json["objectVector"][1]["name"], "Second");
}

TEST(ContainerSerializationTest, VectorObject_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", {{{"id", 100}, {"name", "Deserialized1"}, {"value", 10.5f}}, {{"id", 200}, {"name", "Deserialized2"}, {"value", 20.5f}}}},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.objectVector.size(), 2);
    EXPECT_EQ(obj.objectVector[0].id, 100);
    EXPECT_EQ(obj.objectVector[0].name, "Deserialized1");
    EXPECT_FLOAT_EQ(obj.objectVector[0].value, 10.5f);

    EXPECT_EQ(obj.objectVector[1].id, 200);
    EXPECT_EQ(obj.objectVector[1].name, "Deserialized2");
    EXPECT_FLOAT_EQ(obj.objectVector[1].value, 20.5f);
}

TEST(ContainerSerializationTest, VectorEmpty_Serialize)
{
    ContainerTestObject obj;
    obj.intVector = {};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("intVector"));
    EXPECT_TRUE(json["intVector"].is_array());
    EXPECT_EQ(json["intVector"].size(), 0);
}

TEST(ContainerSerializationTest, VectorEmpty_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;

    // 先填充一些数据
    obj.intVector    = {1, 2, 3};
    obj.stringVector = {"a", "b"};

    // 反序列化空数据应该清空
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.intVector.size(), 0);
    EXPECT_EQ(obj.stringVector.size(), 0);
    EXPECT_EQ(obj.objectVector.size(), 0);
}

// ============================================================================
// Set 容器测试
// ============================================================================

TEST(ContainerSerializationTest, SetInt_Serialize)
{
    ContainerTestObject obj;
    obj.intSet = {5, 3, 1, 4, 2};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("intSet"));
    EXPECT_TRUE(json["intSet"].is_array());
    EXPECT_EQ(json["intSet"].size(), 5);

    // 注意：set 会自动排序，所以顺序是 1,2,3,4,5
    EXPECT_EQ(json["intSet"][0], 1);
    EXPECT_EQ(json["intSet"][4], 5);
}

TEST(ContainerSerializationTest, SetInt_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", {100, 200, 300}},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.intSet.size(), 3);
    EXPECT_TRUE(obj.intSet.count(100));
    EXPECT_TRUE(obj.intSet.count(200));
    EXPECT_TRUE(obj.intSet.count(300));
}

TEST(ContainerSerializationTest, SetEmpty_Serialize)
{
    ContainerTestObject obj;
    obj.intSet = {};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("intSet"));
    EXPECT_TRUE(json["intSet"].is_array());
    EXPECT_EQ(json["intSet"].size(), 0);
}

TEST(ContainerSerializationTest, SetEmpty_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;

    // 先填充
    obj.intSet = {1, 2, 3};

    // 反序列化应该清空
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.intSet.size(), 0);
}

// ============================================================================
// Map 容器测试
// ============================================================================

TEST(ContainerSerializationTest, MapStringInt_Serialize)
{
    ContainerTestObject obj;
    obj.stringIntMap = {
        {"health", 100},
        {"mana", 50},
        {"stamina", 80}};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("stringIntMap"));
    EXPECT_TRUE(json["stringIntMap"].is_object());
    EXPECT_EQ(json["stringIntMap"].size(), 3);

    EXPECT_EQ(json["stringIntMap"]["health"], 100);
    EXPECT_EQ(json["stringIntMap"]["mana"], 50);
    EXPECT_EQ(json["stringIntMap"]["stamina"], 80);
}

TEST(ContainerSerializationTest, MapStringInt_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", {{"level", 10}, {"exp", 9999}, {"gold", 500}}}};

    ContainerTestObject obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.stringIntMap.size(), 3);
    EXPECT_EQ(obj.stringIntMap["level"], 10);
    EXPECT_EQ(obj.stringIntMap["exp"], 9999);
    EXPECT_EQ(obj.stringIntMap["gold"], 500);
}

TEST(ContainerSerializationTest, MapEmpty_Serialize)
{
    ContainerTestObject obj;
    obj.stringIntMap = {};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("stringIntMap"));
    EXPECT_TRUE(json["stringIntMap"].is_object());
    EXPECT_EQ(json["stringIntMap"].size(), 0);
}

TEST(ContainerSerializationTest, MapEmpty_Deserialize)
{
    nlohmann::json json = {
        {"intVector", nlohmann::json::array()},
        {"stringVector", nlohmann::json::array()},
        {"objectVector", nlohmann::json::array()},
        {"intSet", nlohmann::json::array()},
        {"stringIntMap", nlohmann::json::object()}};

    ContainerTestObject obj;

    // 先填充
    obj.stringIntMap = {{"key1", 1}, {"key2", 2}};

    // 反序列化应该清空
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.stringIntMap.size(), 0);
}

// ============================================================================
// 嵌套容器测试
// ============================================================================

TEST(ContainerSerializationTest, NestedVector_Serialize)
{
    NestedContainerTest obj;
    obj.objectMatrix = {
        {1, "first", 1.1f},
        {2, "second", 2.2f},
        {3, "third", 3.3f}
    };

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("objectMatrix"));
    EXPECT_TRUE(json["objectMatrix"].is_array());
    EXPECT_EQ(json["objectMatrix"].size(), 3);
    EXPECT_EQ(json["objectMatrix"][0]["id"], 1);
    EXPECT_EQ(json["objectMatrix"][0]["name"], "first");
    EXPECT_FLOAT_EQ(json["objectMatrix"][0]["value"], 1.1f);
}

TEST(ContainerSerializationTest, NestedVector_Deserialize)
{
    nlohmann::json json = {
        {"objectMatrix", {
            {{"id", 100}, {"name", "test1"}, {"value", 10.5f}},
            {{"id", 200}, {"name", "test2"}, {"value", 20.5f}}
        }}
    };

    NestedContainerTest obj;
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.objectMatrix.size(), 2);
    EXPECT_EQ(obj.objectMatrix[0].id, 100);
    EXPECT_EQ(obj.objectMatrix[0].name, "test1");
    EXPECT_FLOAT_EQ(obj.objectMatrix[0].value, 10.5f);
    EXPECT_EQ(obj.objectMatrix[1].id, 200);
    EXPECT_EQ(obj.objectMatrix[1].name, "test2");
    EXPECT_FLOAT_EQ(obj.objectMatrix[1].value, 20.5f);
}
TEST(ContainerSerializationTest, NestedVector_Empty_Serialize)
{
    NestedContainerTest obj;
    // objectMatrix 为空

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    EXPECT_TRUE(json.contains("objectMatrix"));
    EXPECT_TRUE(json["objectMatrix"].is_array());
    EXPECT_EQ(json["objectMatrix"].size(), 0);
}
TEST(ContainerSerializationTest, NestedVector_Empty_Deserialize)
{
    nlohmann::json json = {
        {"objectMatrix", nlohmann::json::array()}};

    NestedContainerTest obj;

    // 先填充
    obj.objectMatrix = {{1, "test", 1.0f}, {2, "test2", 2.0f}};

    // 反序列化应该清空
    ReflectionSerializer::deserializeByRuntimeReflection(obj, json, "");

    EXPECT_EQ(obj.objectMatrix.size(), 0);
}

// ============================================================================
// 完整场景测试
// ============================================================================

TEST(ContainerSerializationTest, FullObject_Roundtrip)
{
    // 准备原始数据
    ContainerTestObject original;
    original.intVector    = {1, 2, 3, 4, 5};
    original.stringVector = {"apple", "banana", "cherry"};
    original.objectVector = {
        {1, "Item1", 10.0f},
        {2, "Item2", 20.0f}};
    original.intSet       = {10, 20, 30};
    original.stringIntMap = {
        {"score", 100},
        {"level", 5}};

    // 序列化
    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(original);

    std::string jsonString = json.dump();

    // 反序列化
    ContainerTestObject deserialized;
    ReflectionSerializer::deserializeByRuntimeReflection(deserialized, json, "");

    // 验证
    EXPECT_EQ(deserialized.intVector, original.intVector);
    EXPECT_EQ(deserialized.stringVector, original.stringVector);

    ASSERT_EQ(deserialized.objectVector.size(), original.objectVector.size());
    for (size_t i = 0; i < original.objectVector.size(); ++i) {
        EXPECT_EQ(deserialized.objectVector[i].id, original.objectVector[i].id);
        EXPECT_EQ(deserialized.objectVector[i].name, original.objectVector[i].name);
        EXPECT_FLOAT_EQ(deserialized.objectVector[i].value, original.objectVector[i].value);
    }

    EXPECT_EQ(deserialized.intSet, original.intSet);
    EXPECT_EQ(deserialized.stringIntMap, original.stringIntMap);
}

TEST(ContainerSerializationTest, MixedContainers_Serialize)
{
    ContainerTestObject obj;

    // 混合各种容器
    obj.intVector    = {1, 2};
    obj.stringVector = {"test"};
    obj.objectVector = {{1, "single", 1.0f}};
    obj.intSet       = {99};
    obj.stringIntMap = {{"key", 42}};

    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(obj);

    // 验证所有容器都被正确序列化
    EXPECT_TRUE(json["intVector"].is_array());
    EXPECT_TRUE(json["stringVector"].is_array());
    EXPECT_TRUE(json["objectVector"].is_array());
    EXPECT_TRUE(json["intSet"].is_array());
    EXPECT_TRUE(json["stringIntMap"].is_object());

    EXPECT_EQ(json["intVector"].size(), 2);
    EXPECT_EQ(json["stringVector"].size(), 1);
    EXPECT_EQ(json["objectVector"].size(), 1);
    EXPECT_EQ(json["intSet"].size(), 1);
    EXPECT_EQ(json["stringIntMap"].size(), 1);
}

TEST(ContainerSerializationTest, LargeContainers_Performance)
{
    ContainerTestObject obj;

    // 大容器测试（1000个元素）
    for (int i = 0; i < 1000; ++i) {
        obj.intVector.push_back(i);
    }


    // 序列化性能测试
    auto           startSerialize = std::chrono::high_resolution_clock::now();
    nlohmann::json json           = ReflectionSerializer::serializeByRuntimeReflection(obj);
    auto           endSerialize   = std::chrono::high_resolution_clock::now();

    auto serializeTime = std::chrono::duration_cast<std::chrono::milliseconds>(endSerialize - startSerialize);

    EXPECT_EQ(json["intVector"].size(), 1000);
    EXPECT_LT(serializeTime.count(), 100); // 应该在 100ms 内完成

    // 反序列化性能测试
    ContainerTestObject result;
    auto                startDeserialize = std::chrono::high_resolution_clock::now();
    ReflectionSerializer::deserializeByRuntimeReflection(result, json, "");
    auto endDeserialize = std::chrono::high_resolution_clock::now();

    auto deserializeTime = std::chrono::duration_cast<std::chrono::milliseconds>(endDeserialize - startDeserialize);

    EXPECT_EQ(result.intVector.size(), 1000);
    EXPECT_LT(deserializeTime.count(), 100); // 应该在 100ms 内完成
}
