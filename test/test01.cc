
#include "spdlog/spdlog.h"
#include <functional>
#include <glinternal/core.hpp>
#include <gtest/gtest.h>
#include <logx/spdx.hpp>
#include <pch/gl.h>



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
