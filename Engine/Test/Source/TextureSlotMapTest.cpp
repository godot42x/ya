/**
 * @file TextureSlotMapTest.cpp
 * @brief 测试 TextureSlotMap 的序列化和反序列化
 */

#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Render/Material/Material.h"
#include <gtest/gtest.h>

using namespace ya;

// 测试结构体，模拟 PhongMaterialComponent 的 _textureSlots 字段
struct TestTextureSlotContainer
{
    YA_REFLECT_BEGIN(TestTextureSlotContainer)
    YA_REFLECT_FIELD(textureSlots)
    YA_REFLECT_END()

    TextureSlotMap textureSlots;
};

class TextureSlotMapTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TextureSlotMapTest, SerializeTextureSlotMap)
{
    TestTextureSlotContainer container;
    
    // 添加测试数据
    container.textureSlots[0] = TextureSlot("Engine/Content/TestTextures/LearnOpenGL/container2.png");
    container.textureSlots[0].uvScale = {1.0f, 1.0f};
    container.textureSlots[0].uvOffset = {0.0f, 0.0f};
    container.textureSlots[0].uvRotation = 0.0f;
    container.textureSlots[0].bEnable = true;

    container.textureSlots[1] = TextureSlot("Engine/Content/TestTextures/LearnOpenGL/container2_specular.png");
    container.textureSlots[1].uvScale = {1.0f, 1.0f};
    container.textureSlots[1].uvOffset = {0.0f, 0.0f};
    container.textureSlots[1].uvRotation = 0.0f;
    container.textureSlots[1].bEnable = true;

    // 序列化
    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(container);

    std::cout << "Serialized JSON:\n" << json.dump(2) << std::endl;

    // 验证序列化结果
    EXPECT_TRUE(json.contains("textureSlots"));
    EXPECT_TRUE(json["textureSlots"].is_object());
    EXPECT_EQ(json["textureSlots"].size(), 2);

    // 验证具体内容
    EXPECT_TRUE(json["textureSlots"].contains("0"));
    EXPECT_TRUE(json["textureSlots"].contains("1"));
    
    auto& slot0 = json["textureSlots"]["0"];
    EXPECT_TRUE(slot0.contains("textureRef"));
    EXPECT_TRUE(slot0.contains("bEnable"));
    EXPECT_TRUE(slot0.contains("uvScale"));
    EXPECT_TRUE(slot0.contains("uvOffset"));
    EXPECT_TRUE(slot0.contains("uvRotation"));
}

TEST_F(TextureSlotMapTest, DeserializeTextureSlotMap)
{
    // 准备测试 JSON 数据
    nlohmann::json json = {
        {"textureSlots", {
            {"0", {
                {"bEnable", true},
                {"textureRef", {{"_path", "Engine/Content/TestTextures/LearnOpenGL/container2.png"}}},
                {"uvOffset", {{"x", 0.0}, {"y", 0.0}}},
                {"uvRotation", 0.0},
                {"uvScale", {{"x", 1.0}, {"y", 1.0}}}
            }},
            {"1", {
                {"bEnable", true},
                {"textureRef", {{"_path", "Engine/Content/TestTextures/LearnOpenGL/container2_specular.png"}}},
                {"uvOffset", {{"x", 0.0}, {"y", 0.0}}},
                {"uvRotation", 0.0},
                {"uvScale", {{"x", 1.0}, {"y", 1.0}}}
            }}
        }}
    };

    std::cout << "Input JSON:\n" << json.dump(2) << std::endl;

    // 检查反射注册情况
    auto &registry = ClassRegistry::instance();
    auto *containerClass = registry.getClass(ya::type_index_v<TestTextureSlotContainer>);
    std::cout << "Container class found: " << (containerClass ? "YES" : "NO") << std::endl;
    
    if (containerClass) {
        auto *textureSlotsProp = containerClass->getProperty("textureSlots");
        std::cout << "textureSlots property found: " << (textureSlotsProp ? "YES" : "NO") << std::endl;
        
        if (textureSlotsProp) {
            bool isContainer = ::ya::reflection::PropertyContainerHelper::isContainer(*textureSlotsProp);
            std::cout << "textureSlots is container: " << (isContainer ? "YES" : "NO") << std::endl;
            
            if (isContainer) {
                auto *accessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(*textureSlotsProp);
                std::cout << "Container accessor found: " << (accessor ? "YES" : "NO") << std::endl;
                
                if (accessor) {
                    std::cout << "Container type: " << static_cast<int>(accessor->getContainerType()) << std::endl;
                    std::cout << "Is map-like: " << (accessor->isMapLike() ? "YES" : "NO") << std::endl;
                    std::cout << "Key type index: " << accessor->getKeyTypeIndex() << std::endl;
                    std::cout << "Value type index: " << accessor->getElementTypeIndex() << std::endl;
                    
                    // 检查 TextureSlot 类型是否注册
                    auto *textureSlotClass = registry.getClass(accessor->getElementTypeIndex());
                    std::cout << "TextureSlot class found: " << (textureSlotClass ? "YES" : "NO") << std::endl;
                    if (textureSlotClass) {
                        std::cout << "TextureSlot class name: " << textureSlotClass->_name << std::endl;
                        std::cout << "Can create instance: " << (textureSlotClass->canCreateInstance() ? "YES" : "NO") << std::endl;
                    }
                }
            }
        }
    }

    // 反序列化
    TestTextureSlotContainer container;
    ReflectionSerializer::deserializeByRuntimeReflection(container, json, "");

    std::cout << "After deserialization, textureSlots size: " << container.textureSlots.size() << std::endl;

    // 验证反序列化结果
    EXPECT_EQ(container.textureSlots.size(), 2);
    
    if (container.textureSlots.size() >= 2) {
        EXPECT_TRUE(container.textureSlots.contains(0));
        EXPECT_TRUE(container.textureSlots.contains(1));
        
        auto& slot0 = container.textureSlots[0];
        EXPECT_EQ(slot0.textureRef._path, "Engine/Content/TestTextures/LearnOpenGL/container2.png");
        EXPECT_TRUE(slot0.bEnable);
        EXPECT_EQ(slot0.uvScale.x, 1.0f);
        EXPECT_EQ(slot0.uvScale.y, 1.0f);
        
        auto& slot1 = container.textureSlots[1];
        EXPECT_EQ(slot1.textureRef._path, "Engine/Content/TestTextures/LearnOpenGL/container2_specular.png");
        EXPECT_TRUE(slot1.bEnable);
    }
}

TEST_F(TextureSlotMapTest, RoundtripTest)
{
    // 原始数据
    TestTextureSlotContainer original;
    original.textureSlots[0] = TextureSlot("test1.png");
    original.textureSlots[1] = TextureSlot("test2.png");

    // 序列化
    nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(original);
    
    // 反序列化
    TestTextureSlotContainer deserialized;
    ReflectionSerializer::deserializeByRuntimeReflection(deserialized, json, "");

    // 验证往返一致性
    EXPECT_EQ(deserialized.textureSlots.size(), original.textureSlots.size());
    EXPECT_EQ(deserialized.textureSlots[0].textureRef._path, original.textureSlots[0].textureRef._path);
    EXPECT_EQ(deserialized.textureSlots[1].textureRef._path, original.textureSlots[1].textureRef._path);
}