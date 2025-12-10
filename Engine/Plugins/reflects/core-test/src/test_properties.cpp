#include "test_common.h"

// ============================================================================
// Static Property Tests
// ============================================================================

TEST(StaticPropertyTest, AccessStaticProperty)
{
    Class* configClass = ClassRegistry::instance().getClass("ConfigManager");
    ASSERT_NE(configClass, nullptr);
    
    // 读取静态属性
    int maxConn = configClass->getStaticPropertyValue<int>("maxConnections");
    EXPECT_EQ(maxConn, 100);
    
    int timeout = configClass->getStaticPropertyValue<int>("defaultTimeout");
    EXPECT_EQ(timeout, 30);
}

TEST(StaticPropertyTest, ModifyStaticProperty)
{
    Class* configClass = ClassRegistry::instance().getClass("ConfigManager");
    ASSERT_NE(configClass, nullptr);
    
    // 修改静态属性
    configClass->setStaticPropertyValue("maxConnections", 200);
    EXPECT_EQ(ConfigManager::maxConnections, 200);
    
    // 通过反射读取验证
    int maxConn = configClass->getStaticPropertyValue<int>("maxConnections");
    EXPECT_EQ(maxConn, 200);
    
    // 恢复原值
    ConfigManager::maxConnections = 100;
}

TEST(StaticPropertyTest, ConstStaticPropertyReadOnly)
{
    Class* configClass = ClassRegistry::instance().getClass("ConfigManager");
    ASSERT_NE(configClass, nullptr);
    
    // 读取const静态属性应该成功
    int timeout = configClass->getStaticPropertyValue<int>("defaultTimeout");
    EXPECT_EQ(timeout, 30);
    
    // 修改const静态属性应该失败
    EXPECT_THROW(
        configClass->setStaticPropertyValue("defaultTimeout", 60),
        std::runtime_error
    );
}

TEST(StaticPropertyTest, PropertyIntrospection)
{
    Class* configClass = ClassRegistry::instance().getClass("ConfigManager");
    ASSERT_NE(configClass, nullptr);
    
    // 检查属性是否存在
    EXPECT_TRUE(configClass->hasProperty("maxConnections"));
    EXPECT_TRUE(configClass->hasProperty("defaultTimeout"));
    
    // 获取属性元数据
    const Property* prop = configClass->getProperty("maxConnections");
    ASSERT_NE(prop, nullptr);
    EXPECT_TRUE(prop->bStatic);
    EXPECT_FALSE(prop->bConst);
    
    const Property* constProp = configClass->getProperty("defaultTimeout");
    ASSERT_NE(constProp, nullptr);
    EXPECT_TRUE(constProp->bStatic);
    EXPECT_TRUE(constProp->bConst);
}
