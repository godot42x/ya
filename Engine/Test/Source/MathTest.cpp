// 测试Math.h中的3x3矩阵相关功能
#include "Core/Math/Math.h"
#include <gtest/gtest.h>

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>


class MathTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}

    // 辅助函数：比较两个3x3矩阵是否近似相等
    static bool isMatrix3x3Equal(const glm::mat3& a, const glm::mat3& b, float epsilon = glm::epsilon<float>())
    {
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 3; ++row) {
                if (std::fabs(a[col][row] - b[col][row]) > epsilon) {
                    return false;
                }
            }
        }
        return true;
    }};

// 测试缩放矩阵
TEST_F(MathTest, BuildScaleMatrix3x3)
{
    // 测试单位缩放
    glm::vec2 scale1(1.0f, 1.0f);
    glm::mat3 expected1 = glm::mat3(1.0f);
    glm::mat3 result1   = ya::FMath::build_scale_mat3(scale1);
    EXPECT_TRUE(isMatrix3x3Equal(result1, expected1)) << "单位缩放应该返回单位矩阵";

    // 测试均匀缩放
    glm::vec2 scale2(2.0f, 2.0f);
    glm::mat3 expected2 = glm::mat3{
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    glm::mat3 result2 = ya::FMath::build_scale_mat3(scale2);
    EXPECT_TRUE(isMatrix3x3Equal(result2, expected2)) << "均匀缩放2x应该正确";

    // 测试非均匀缩放
    glm::vec2 scale3(3.0f, 4.0f);
    glm::mat3 expected3 = glm::mat3{
        3.0f, 0.0f, 0.0f,
        0.0f, 4.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    glm::mat3 result3 = ya::FMath::build_scale_mat3(scale3);
    EXPECT_TRUE(isMatrix3x3Equal(result3, expected3)) << "非均匀缩放应该正确";
}

// 测试旋转矩阵
TEST_F(MathTest, BuildRotateMatrix3x3)
{
    // 测试0度旋转
    glm::mat3 result0   = ya::FMath::build_rotate_mat3(0.0f);
    glm::mat3 expected0 = glm::mat3(1.0f);
    EXPECT_TRUE(isMatrix3x3Equal(result0, expected0)) << "0度旋转应该返回单位矩阵";

    // 测试90度旋转
    glm::mat3 result90   = ya::FMath::build_rotate_mat3(90.0f);
    glm::mat3 expected90 = glm::mat3{
        0.0f, -1.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(result90, expected90)) << "90度旋转应该正确";

    // 测试45度旋转
    glm::mat3 result45   = ya::FMath::build_rotate_mat3(45.0f);
    float     cos45      = std::cos(glm::radians(45.0f));
    float     sin45      = std::sin(glm::radians(45.0f));
    glm::mat3 expected45 = glm::mat3{
        cos45, -sin45, 0.0f,
        sin45, cos45, 0.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(result45, expected45)) << "45度旋转应该正确";

    // 测试负角度旋转
    glm::mat3 resultNeg30   = ya::FMath::build_rotate_mat3(-30.0f);
    float     cosNeg30      = std::cos(glm::radians(-30.0f));
    float     sinNeg30      = std::sin(glm::radians(-30.0f));
    glm::mat3 expectedNeg30 = glm::mat3{
        cosNeg30, -sinNeg30, 0.0f,
        sinNeg30, cosNeg30, 0.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(resultNeg30, expectedNeg30)) << "负角度旋转应该正确";
}

// 测试平移矩阵
TEST_F(MathTest, BuildTranslateMatrix3x3)
{
    // 测试零平移
    glm::vec2 trans1(0.0f, 0.0f);
    glm::mat3 result1   = ya::FMath::build_translate_mat3(trans1);
    glm::mat3 expected1 = glm::mat3(1.0f);
    EXPECT_TRUE(isMatrix3x3Equal(result1, expected1)) << "零平移应该返回单位矩阵";

    // 测试x方向平移
    glm::vec2 trans2(5.0f, 0.0f);
    glm::mat3 result2   = ya::FMath::build_translate_mat3(trans2);
    glm::mat3 expected2 = glm::mat3{
        1.0f, 0.0f, 5.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(result2, expected2)) << "x方向平移应该正确";

    // 测试y方向平移
    glm::vec2 trans3(0.0f, 7.0f);
    glm::mat3 result3   = ya::FMath::build_translate_mat3(trans3);
    glm::mat3 expected3 = glm::mat3{
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 7.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(result3, expected3)) << "y方向平移应该正确";

    // 测试xy方向平移
    glm::vec2 trans4(3.0f, 4.0f);
    glm::mat3 result4   = ya::FMath::build_translate_mat3(trans4);
    glm::mat3 expected4 = glm::mat3{
        1.0f, 0.0f, 3.0f,
        0.0f, 1.0f, 4.0f,
        0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(isMatrix3x3Equal(result4, expected4)) << "xy方向平移应该正确";
}

// 测试组合变换矩阵
TEST_F(MathTest, BuildTransformMatrix3x3)
{
    // 测试只平移
    glm::vec2 trans(2.0f, 3.0f);
    float     rot = 0.0f;
    glm::vec2 scale(1.0f, 1.0f);

    glm::mat3 result   = ya::FMath::build_transform_mat3(trans, rot, scale);
    glm::mat3 expected = ya::FMath::build_translate_mat3(trans);
    EXPECT_TRUE(isMatrix3x3Equal(result, expected)) << "只平移时变换矩阵应该等于平移矩阵";

    // 测试只旋转
    glm::vec2 trans2(0.0f, 0.0f);
    float     rot2 = 45.0f;
    glm::vec2 scale2(1.0f, 1.0f);

    glm::mat3 result2   = ya::FMath::build_transform_mat3(trans2, rot2, scale2);
    glm::mat3 expected2 = ya::FMath::build_rotate_mat3(rot2);
    EXPECT_TRUE(isMatrix3x3Equal(result2, expected2)) << "只旋转时变换矩阵应该等于旋转矩阵";

    // 测试只缩放
    glm::vec2 trans3(0.0f, 0.0f);
    float     rot3 = 0.0f;
    glm::vec2 scale3(2.0f, 3.0f);

    glm::mat3 result3   = ya::FMath::build_transform_mat3(trans3, rot3, scale3);
    glm::mat3 expected3 = ya::FMath::build_scale_mat3(scale3);
    EXPECT_TRUE(isMatrix3x3Equal(result3, expected3)) << "只缩放时变换矩阵应该等于缩放矩阵";

    // 测试组合变换
    glm::vec2 trans4(1.0f, 2.0f);
    float     rot4 = 30.0f;
    glm::vec2 scale4(2.0f, 3.0f);

    glm::mat3 result4 = ya::FMath::build_transform_mat3(trans4, rot4, scale4);

    // 验证组合变换的数学正确性：T * R * S
    float rad = glm::radians(rot4);
    float c   = std::cos(rad);
    float s   = std::sin(rad);

    glm::mat3 expected4 = glm::mat3{
        scale4.x * c, -scale4.y * s, trans4.x,
        scale4.x * s, scale4.y * c, trans4.y,
        0.0f, 0.0f, 1.0f};

    EXPECT_TRUE(isMatrix3x3Equal(result4, expected4)) << "组合变换矩阵应该正确";
}

// 测试向量常量
TEST_F(MathTest, VectorConstants)
{
    // 测试右手坐标系
    EXPECT_TRUE(ya::FMath::Vector::IsRightHanded);

    // 测试列主序
    EXPECT_TRUE(ya::FMath::Vector::ColumnMajor);

    // 测试世界方向向量
    glm::vec3 worldUp = ya::FMath::Vector::WorldUp;
    EXPECT_FLOAT_EQ(worldUp.x, 0.0f);
    EXPECT_FLOAT_EQ(worldUp.y, 1.0f);
    EXPECT_FLOAT_EQ(worldUp.z, 0.0f);

    glm::vec3 worldRight = ya::FMath::Vector::WorldRight;
    EXPECT_FLOAT_EQ(worldRight.x, 1.0f);
    EXPECT_FLOAT_EQ(worldRight.y, 0.0f);
    EXPECT_FLOAT_EQ(worldRight.z, 0.0f);

    glm::vec3 worldForward = ya::FMath::Vector::WorldForward;
    EXPECT_FLOAT_EQ(worldForward.x, 0.0f);
    EXPECT_FLOAT_EQ(worldForward.y, 0.0f);
    EXPECT_FLOAT_EQ(worldForward.z, -1.0f);
}
