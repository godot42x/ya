
#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>


namespace ya
{

/**
 * @brief Coordinate system handedness
 */
enum class CoordinateSystem
{
    LeftHanded,  // DirectX, Unity (Z+ forward into screen)
    RightHanded, // OpenGL, Vulkan, Blender (Z+ backward toward viewer)
};


// Define our engine's target coordinate system
// Change this if you switch rendering backends or coordinate conventions
inline constexpr CoordinateSystem ENGINE_COORDINATE_SYSTEM = CoordinateSystem::RightHanded;

struct Vertex
{
    glm::vec3 position;
    glm::vec2 texCoord0;
    glm::vec3 normal;
};


/**
 * @brief 快速几何体生成器 - 预定义常用形状
 *
 * 所有几何体默认生成在原点，使用 ENGINE_COORDINATE_SYSTEM 坐标系
 * 包含位置、法线、UV坐标
 */
struct PrimitiveGeometry
{
    /**
     * @brief 生成单位立方体 (边长为1，中心在原点)
     * @param outVertices 输出顶点数组
     * @param outIndices 输出索引数组
     */
    static void createCube(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成缩放立方体
     * @param size 立方体的尺寸 (x, y, z)
     */
    static void createCube(const glm::vec3 &size, std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成球体 (UV球)
     * @param radius 半径
     * @param slices 经线数量 (横向分段)
     * @param stacks 纬线数量 (纵向分段)
     */
    static void createSphere(float radius, uint32_t slices, uint32_t stacks,
                             std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成平面 (XZ平面，法线向上+Y)
     * @param width X方向宽度
     * @param depth Z方向深度
     * @param uRepeat U方向UV重复次数
     * @param vRepeat V方向UV重复次数
     */
    static void createPlane(float width, float depth, float uRepeat, float vRepeat,
                            std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成圆柱体
     * @param radius 半径
     * @param height 高度
     * @param segments 圆周分段数
     */
    static void createCylinder(float radius, float height, uint32_t segments,
                               std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成圆锥体
     * @param radius 底面半径
     * @param height 高度
     * @param segments 圆周分段数
     */
    static void createCone(float radius, float height, uint32_t segments,
                           std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);

    /**
     * @brief 生成全屏四边形 (用于后处理)
     * NDC坐标 [-1,1], UV [0,1]
     */
    static void createFullscreenQuad(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices);
};


namespace geo
{

struct Vertex
{
    glm::vec3 postion;
};

struct Edge
{
    Vertex start;
    Vertex end;
};

struct Face
{
    Edge edge1;
    Edge edge2;
    Edge edge3;
};

struct Plane
{
    glm::vec3 normal;
    float     d;

    Plane(const glm::vec3 &normal, float d) : normal(normal), d(d) {}
    [[nodiscard]] float distanceTo(const glm::vec3 &point) const
    {
        return glm::dot(normal, point) + d;
    }
};

} // namespace geo

}; // namespace ya