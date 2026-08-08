/**
 * @file Std140Types.h
 * @brief std140 布局兼容类型 - 直接用于 UBO 定义
 *
 * 设计目标：
 * - 一次定义，CPU/GPU 布局一致
 * - 零 pack 开销，直接 memcpy 到 GPU
 * - 使用语义类型保持可读性
 *
 * 使用示例：
 * @code
 * struct MyMaterialUBO {
 *     std140::vec3 color;       // 自动 16 字节对齐
 *     std140::f32  metallic;    // 4 字节
 *     std140::mat3 uvTransform; // 48 字节 (3x vec4)
 * };
 *
 * MyMaterialUBO ubo;
 * ubo.color = glm::vec3(1, 0, 0);  // 隐式转换
 * buffer->writeData(&ubo, sizeof(ubo));
 * @endcode
 */
#pragma once

#include "Core/Reflection/Reflection.h"
#include "glm/glm.hpp"
#include "stdint.h"
// warning C4324
#pragma warning(push)
#pragma warning(disable : 4324)

namespace ya::std140
{

// ============================================================================
// 标量类型 - 4 字节对齐
// ============================================================================

using f32 = glm::float32_t;
using i32 = int32_t;
using u32 = uint32_t;

struct alignas(4) b32
{
    bool value = false; // GLSL bool is 4 bytes

    b32() = default;
    b32(bool v) : value(v ? 1 : 0) {}
    operator bool() const { return value /*!= 0*/; }
    b32& operator=(bool v)
    {
        // value = v ? 1 : 0;
        value = v;
        return *this;
    }
};


// ============================================================================
// 向量类型 - vec2: 8字节, vec3/vec4: 16字节对齐
// ============================================================================

struct vec2
{
    alignas(8) glm::vec2 value{0.0f};

    vec2() = default;
    vec2(const glm::vec2& v) : value(v) {}
    vec2(float x, float y) : value(x, y) {}
    operator glm::vec2() const { return value; }
    vec2& operator=(const glm::vec2& v)
    {
        value = v;
        return *this;
    }

    float&       operator[](int i) { return value[i]; }
    const float& operator[](int i) const { return value[i]; }
};

struct vec3
{
    alignas(16) glm::vec3 value{0.0f};

    vec3() = default;
    vec3(const glm::vec3& v) : value(v) {}
    vec3(float x, float y, float z) : value(x, y, z) {}
    vec3(float s) : value(s) {}
    operator glm::vec3() const { return value; }
    vec3& operator=(const glm::vec3& v)
    {
        value = v;
        return *this;
    }

    float&       operator[](int i) { return value[i]; }
    const float& operator[](int i) const { return value[i]; }
};

struct vec4
{
    alignas(16) glm::vec4 value{0.0f};

    vec4() = default;
    vec4(const glm::vec4& v) : value(v) {}
    vec4(float x, float y, float z, float w) : value(x, y, z, w) {}
    vec4(const glm::vec3& xyz, float w) : value(xyz, w) {}
    vec4(float s) : value(s) {}
    operator glm::vec4() const { return value; }
    vec4& operator=(const glm::vec4& v)
    {
        value = v;
        return *this;
    }

    float&       operator[](int i) { return value[i]; }
    const float& operator[](int i) const { return value[i]; }
};

struct ivec2
{
    alignas(8) glm::ivec2 value{0};

    ivec2() = default;
    ivec2(const glm::ivec2& v) : value(v) {}
    ivec2(int x, int y) : value(x, y) {}
    operator glm::ivec2() const { return value; }
    ivec2& operator=(const glm::ivec2& v)
    {
        value = v;
        return *this;
    }
};

struct ivec4
{
    alignas(16) glm::ivec4 value{0};

    ivec4() = default;
    ivec4(const glm::ivec4& v) : value(v) {}
    operator glm::ivec4() const { return value; }
    ivec4& operator=(const glm::ivec4& v)
    {
        value = v;
        return *this;
    }
};

// ============================================================================
// 矩阵类型 - 按列存储，每列按 vec4 对齐
// ============================================================================

/**
 * @brief std140 mat3 - 占用 48 字节 (3 x vec4)
 *
 * 注意：GLSL 中 mat3 的每一列按 vec4 对齐
 */
struct alignas(16) mat3
{
    alignas(16) glm::mat3x4 value{1.0f}; // 使用 mat3x4 存储，自动满足 std140 对齐


    mat3(const glm::mat3& m)
    {
        value = m;
    }

    mat3(float diagonal)
    {
        value = glm::mat3x4(
            glm::vec4(diagonal, 0, 0, 0),
            glm::vec4(0, diagonal, 0, 0),
            glm::vec4(0, 0, diagonal, 0));
    }

    operator glm::mat3() const
    {
        return value;
    }

    mat3& operator=(const glm::mat3& m)
    {
        value = m;
        return *this;
    }

    glm::vec4&       operator[](int i) { return value[i]; }
    const glm::vec4& operator[](int i) const { return value[i]; }
};

struct mat4
{
    alignas(16) glm::mat4 value{1.0f};

    mat4() = default;
    mat4(const glm::mat4& m) : value(m) {}
    mat4(float diagonal) : value(diagonal) {}
    operator glm::mat4() const { return value; }
    mat4& operator=(const glm::mat4& m)
    {
        value = m;
        return *this;
    }

    glm::vec4&       operator[](int i) { return value[i]; }
    const glm::vec4& operator[](int i) const { return value[i]; }
};

} // namespace ya::std140

YA_REFLECT_BEGIN_EXTERNAL(ya::std140::b32)
YA_REFLECT_FIELD_SPEC(b32, ya::std140::b32::value)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::std140::vec3)
YA_REFLECT_FIELD_SPEC(vec3, ya::std140::vec3::value)
YA_REFLECT_END_EXTERNAL()


YA_REFLECT_BEGIN_EXTERNAL(ya::std140::vec2)
YA_REFLECT_FIELD_SPEC(vec2, ya::std140::vec2::value)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::std140::vec4)
YA_REFLECT_FIELD_SPEC(vec4, ya::std140::vec4::value)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::std140::mat3)
YA_REFLECT_FIELD_SPEC(mat3, ya::std140::mat3::value)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::std140::mat4)
YA_REFLECT_FIELD_SPEC(mat4, ya::std140::mat4::value)
YA_REFLECT_END_EXTERNAL()


#pragma warning(pop)