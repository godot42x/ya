
#include "spdlog/spdlog.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <glinternal/core.h>
#include <gtest/gtest.h>
#include <logx/spdx.h>
#include <unistd.h>



TEST(TestInputMapping, InsertAndCall)
{
    spdlog::debug("Start inputMapping insert...");
    // TODO: test input mapping
    glinternal::Gloria ctx;

    auto b = ctx.AddInputMapping(GLFW_KEY_0, glinternal::MappingFn([]() { LERROR("hello world"); return true; }));

    EXPECT_EQ(b, true);

     ctx.InputCallback(GLFW_KEY_0);


    sleep(1000);
}
