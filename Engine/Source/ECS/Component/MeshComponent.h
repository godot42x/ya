/**
 * @file MeshComponent.h
 * @brief 通用网格组件 - 纯数据，可序列化
 *
 * 设计原则：
 * - 职责单一：只负责网格数据的引用和加载
 * - 支持两种来源：内置几何体 / 外部模型文件
 * - 运行时缓存自动解析
 * - 与 MaterialComponent 分离，由 System 组织关系
 */
#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Mesh.h"

namespace ya
{

/**
 * @brief 网格组件 - 管理 Entity 的几何网格数据
 *
 * 序列化格式：
 * @code
 * {
 *   "MeshComponent": {
 *     "_primitiveGeometry": "Cube",  // 或
 *     "_modelRef": { "_path": "Content/Models/character.fbx" }
 *   }
 * }
 * @endcode
 */
struct MeshComponent : public IComponent
{
    YA_REFLECT_BEGIN(MeshComponent)
    YA_REFLECT_FIELD(_primitiveGeometry)
    YA_REFLECT_FIELD(_modelRef)
    YA_REFLECT_FIELD(_materialIndex)
    YA_REFLECT_END()

    // ========================================
    // 可序列化数据
    // ========================================
    EPrimitiveGeometry _primitiveGeometry = EPrimitiveGeometry::None; ///< 内置几何体类型
    ModelRef           _modelRef;                                     ///< 外部模型路径
    uint32_t           _materialIndex = 0;

    // ========================================
    // 运行时缓存（不序列化）
    // ========================================
    std::vector<Mesh *> _cachedMeshes; ///< 已加载的网格指针列表（指向 Model 或 PrimitiveCache）
    bool                _bResolved = false;

    // ========================================
    // 资源解析
    // ========================================

    /**
     * @brief 解析网格资源
     * Called by ResourceResolveSystem
     * @return true 如果成功加载
     */
    bool resolve();

    /**
     * @brief 强制重新解析
     */
    void invalidate()
    {
        _bResolved = false;
        _cachedMeshes.clear();
    }

    /**
     * @brief 检查是否已解析
     */
    bool isResolved() const { return _bResolved; }

    // ========================================
    // 网格访问
    // ========================================

    /**
     * @brief 获取网格列表
     */
    const std::vector<Mesh *> &getMeshes() const { return _cachedMeshes; }

    /**
     * @brief 获取第一个网格（便捷接口）
     */
    Mesh *getFirstMesh() const
    {
        return _cachedMeshes.empty() ? nullptr : _cachedMeshes[0];
    }

    /**
     * @brief 获取网格数量
     */
    size_t getMeshCount() const { return _cachedMeshes.size(); }

    // ========================================
    // 设置接口
    // ========================================

    /**
     * @brief 设置为内置几何体
     */
    void setPrimitiveGeometry(EPrimitiveGeometry type)
    {
        _primitiveGeometry = type;
        _modelRef          = ModelRef(); // Clear model ref
        invalidate();
    }

    /**
     * @brief 设置为外部模型
     */
    void setModelPath(const std::string &path)
    {
        _modelRef          = ModelRef(path);
        _primitiveGeometry = EPrimitiveGeometry::None;
        invalidate();
    }

    /**
     * @brief 检查是否有有效的网格来源
     */
    bool hasMeshSource() const
    {
        return _primitiveGeometry != EPrimitiveGeometry::None || _modelRef.hasPath();
    }
};

} // namespace ya
