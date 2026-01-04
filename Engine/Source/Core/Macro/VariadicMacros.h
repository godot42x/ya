/**
 * @file VariadicMacros.h
 * @brief 通用可变参宏工具 - 支持对可变参数列表进行批量操作
 *
 * 这是一个通用的宏工具库，可用于各种需要对可变参数进行批量处理的场景。
 * 例如：反射系统、序列化、代码生成等。
 *
 * 使用方式：
 * 1. 定义操作宏（接受两个参数：Context, Item）
 * 2. 使用 YA_FOREACH(operation, Context, ...) 对所有参数执行操作
 *
 * 示例：
 *     #define PRINT_VAR(Type, var) std::cout << #var << " = " << var << "\n";
 *     YA_FOREACH(PRINT_VAR, MyClass, x, y, z)
 *
 * 当前限制：
 * - 最多支持 16 个参数（可根据需要扩展到 32/64/128）
 * - 宏展开开销随参数增加而增大
 *
 * 未来方向：
 * - 对于需要反射 100+ 成员的复杂类型，建议使用代码生成器（generator）
 * - 配合注解（attribute）系统，生成反射代码
 * - 示例工具：clang libTooling、C++20 reflection (TS)、自定义 AST 解析器
 *
 * @note 如果你的类有超过 16 个成员需要反射，这是设计的坏味道（God Object）
 *       建议重构为多个小类，或使用代码生成工具
 */

#pragma once

// ============================================================================
// 基础宏工具
// ============================================================================

/// 宏展开辅助 - 强制展开嵌套宏
#define YA_EXPAND(...) __VA_ARGS__

/// 字符串化
#define YA_STRINGIFY_IMPL(x) #x
#define YA_STRINGIFY(x) YA_STRINGIFY_IMPL(x)

/// 连接符号
#define YA_CONCAT_IMPL(a, b) a##b
#define YA_CONCAT(a, b) YA_CONCAT_IMPL(a, b)

// ============================================================================
// 可变参数计数与选择
// ============================================================================

/// 获取可变参数数量（支持 1-16）
#define YA_VA_NARGS(...) \
    YA_EXPAND(YA_VA_NARGS_IMPL(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

#define YA_VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

/// 根据参数数量选择对应的宏（1-16）
#define YA_SELECT_MACRO_BY_COUNT(basename, ...) \
    YA_EXPAND(YA_CONCAT(basename, YA_VA_NARGS(__VA_ARGS__)))

// ============================================================================
// 通用 FOREACH 宏系统 - 对每个参数执行指定操作
// ============================================================================

/**
 * @brief 对每个参数执行指定操作
 * @param operation 操作宏，接受两个参数 (Context, Item)
 * @param Context 上下文参数（通常是类型名）
 * @param ... 可变参数列表（1-16个）
 *
 * 示例：
 *     #define MY_OP(Type, item) std::cout << item;
 *     YA_FOREACH(MY_OP, int, a, b, c)
 *     // 展开为: MY_OP(int, a) MY_OP(int, b) MY_OP(int, c)
 */

// 核心实现宏（1-16）
#define YA_FOREACH_1(op, ctx, p1) op(ctx, p1)

#define YA_FOREACH_2(op, ctx, p1, p2) \
    YA_FOREACH_1(op, ctx, p1)         \
    op(ctx, p2)

#define YA_FOREACH_3(op, ctx, p1, p2, p3) \
    YA_FOREACH_2(op, ctx, p1, p2)         \
    op(ctx, p3)

#define YA_FOREACH_4(op, ctx, p1, p2, p3, p4) \
    YA_FOREACH_3(op, ctx, p1, p2, p3)         \
    op(ctx, p4)

#define YA_FOREACH_5(op, ctx, p1, p2, p3, p4, p5) \
    YA_FOREACH_4(op, ctx, p1, p2, p3, p4)         \
    op(ctx, p5)

#define YA_FOREACH_6(op, ctx, p1, p2, p3, p4, p5, p6) \
    YA_FOREACH_5(op, ctx, p1, p2, p3, p4, p5)         \
    op(ctx, p6)

#define YA_FOREACH_7(op, ctx, p1, p2, p3, p4, p5, p6, p7) \
    YA_FOREACH_6(op, ctx, p1, p2, p3, p4, p5, p6)         \
    op(ctx, p7)

#define YA_FOREACH_8(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8) \
    YA_FOREACH_7(op, ctx, p1, p2, p3, p4, p5, p6, p7)         \
    op(ctx, p8)

#define YA_FOREACH_9(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9) \
    YA_FOREACH_8(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8)         \
    op(ctx, p9)

#define YA_FOREACH_10(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10) \
    YA_FOREACH_9(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9)           \
    op(ctx, p10)

#define YA_FOREACH_11(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11) \
    YA_FOREACH_10(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10)          \
    op(ctx, p11)

#define YA_FOREACH_12(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12) \
    YA_FOREACH_11(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11)          \
    op(ctx, p12)

#define YA_FOREACH_13(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13) \
    YA_FOREACH_12(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12)          \
    op(ctx, p13)

#define YA_FOREACH_14(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14) \
    YA_FOREACH_13(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13)          \
    op(ctx, p14)

#define YA_FOREACH_15(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15) \
    YA_FOREACH_14(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14)          \
    op(ctx, p15)

#define YA_FOREACH_16(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16) \
    YA_FOREACH_15(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15)          \
    op(ctx, p16)
#define YA_FOREACH_17(op, ctx, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17) \
    static_assert(false, "YA_FOREACH only supports up to 16 parameters. For more parameters, consider using code generation or refactoring your design.")

// 通用接口 - 自动选择对应数量的宏
#define YA_FOREACH(operation, context, ...) \
    YA_EXPAND(YA_SELECT_MACRO_BY_COUNT(YA_FOREACH_, __VA_ARGS__)(operation, context, __VA_ARGS__))

// ============================================================================
// 辅助宏：参数选择器（兼容旧版本）
// ============================================================================

/// 手动选择宏（1-16参数）- 用于需要明确指定数量的场景
#define YA_GET_MACRO_16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, NAME, ...) NAME

// ============================================================================
// 使用示例（仅作文档用途）
// ============================================================================

#if 0
// 示例 1：打印所有变量
    #define PRINT_VAR(Type, var) printf("%s = %d\n", #var, var);
void example1() {
    int a = 1, b = 2, c = 3;
    YA_FOREACH(PRINT_VAR, int, a, b, c)
}

// 示例 2：反射系统
    #define REGISTER_FIELD(ClassName, field) \
        registry.addField(#ClassName, #field, &ClassName::field);

struct Player {
    int health, mana, level;
    
    static void registerReflection() {
        YA_FOREACH(REGISTER_FIELD, Player, health, mana, level)
    }
};

// 示例 3：序列化
    #define SERIALIZE_FIELD(Type, field) \
        archive & field;

struct SaveData {
    int score;
    float time;
    
    template<typename Archive>
    void serialize(Archive& archive) {
        YA_FOREACH(SERIALIZE_FIELD, SaveData, score, time)
    }
};
#endif
