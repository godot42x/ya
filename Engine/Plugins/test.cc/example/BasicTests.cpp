#include "test.cc/framework.h"

TEST_CASE(SimpleTest)
{
    return true;
}

TEST_CASE(BasicMath)
{
    int a = 2 + 2;
    TEST_ASSERT_EQ(4, a);
    return true;
}

TEST_CASE(StringTest)
{
    std::string hello    = "Hello";
    std::string world    = "World";
    std::string combined = hello + " " + world;
    TEST_ASSERT_EQ("Hello World", combined);
    return true;
}
