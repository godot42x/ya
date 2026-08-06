#include <gtest/gtest.h>

#include <quickjs.h>

namespace ya
{

// Minimal integration check for the quickjs-ng dependency: create a runtime,
// evaluate a snippet and read the result back through the public C API.
TEST(QuickJSSmokeTest, EvalAddExpression)
{
    JSRuntime* runtime = JS_NewRuntime();
    ASSERT_NE(runtime, nullptr);

    JSContext* context = JS_NewContext(runtime);
    ASSERT_NE(context, nullptr);

    const char* source   = "1 + 2";
    JSValue     result   = JS_Eval(context, source, strlen(source), "<smoke>", JS_EVAL_TYPE_GLOBAL);
    ASSERT_FALSE(JS_IsException(result)) << "JS_Eval failed";

    int32_t value = 0;
    ASSERT_EQ(JS_ToInt32(context, &value, result), 0);
    EXPECT_EQ(value, 3);

    JS_FreeValue(context, result);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

} // namespace ya
