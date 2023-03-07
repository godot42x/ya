#include "glinternal/Core.h"
#include "logx/spdx.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <gtest/gtest.h>



TEST(TestInputMapping, test01)
{
    // TODO: test input mapping
    glinternal::Gloria ctx;

    auto b = ctx.AddInputMapping(
        GLFW_KEY_0, std::function([]() { LERROR("hello world"); return true; }));
    EXPECT_EQ(b, true);

    ctx.InputCallback(GLFW_KEY_0);
}
