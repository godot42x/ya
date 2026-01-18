/**
 * @file MeshComponent.h
 * @brief 通用网格组件 - 纯数据，可序列化
 *
 * 设计原则：
 * - 职责单一：只负责网格数据的引用和加载
 * - 支持两种来源：内置几何体 / 外部模型文件
 * - 运行时缓存自动解析
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
    YA_REFLECT_END()

    // ========================================
    // 可序列化数据
    // ========================================
    EPrimitiveGeometry _primitiveGeometry = EPrimitiveGeometry::None; ///< 内置几何体类型
    ModelRef           _modelRef;                                     ///< 外部模型路径

    // ========================================
    // 运行时缓存（不序列化）
    // ========================================
    std::vector<stdptr<Mesh>> _cachedMeshes; ///< 已加载的网格列表（多 SubMesh 支持）
    bool                      _bResolved = false;

    /**
     * @brief 解析网格资源（从路径加载或生成内置几何体）
     * @return true 如果成功加载
     */
    bool resolve()
    {
        if (_bResolved) return true;

        _cachedMeshes.clear();

        // 优先使用内置几何体
        if (_primitiveGeometry != EPrimitiveGeometry::None) {
            // TODO: 实现 Mesh::createPrimitive 工厂方法
            // auto mesh = Mesh::createPrimitive(_primitiveGeometry);
            YA_CORE_ERROR("Primitive geometry not yet implemented: {}", (int)_primitiveGeometry);
            return false;
        }

        // 尝试加载外部模型
        if (_modelRef.hasPath()) {
            // TODO: 实现 AssetManager::loadMeshes
            // auto loadedMeshes = AssetManager::get()->loadMeshes(_modelRef.getPath());
            YA_CORE_ERROR("Model loading not yet implemented: {}", _modelRef.getPath());
            return false;
        }

        YA_CORE_WARN("MeshComponent has no geometry source");
        return false;
    }

    /**
     * @brief 强制重新解析（当路径或几何体类型变更时调用）
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

    /**
     * @brief 获取网格列表（可能包含多个 SubMesh）
     */
    const std::vector<stdptr<Mesh>> &getMeshes() const { return _cachedMeshes; }

    /**
     * @brief 获取第一个网格（便捷接口，适用于单网格对象）
     */
    Mesh *getFirstMesh() const
    {
        return _cachedMeshes.empty() ? nullptr : _cachedMeshes[0].get();
    }
};

} // namespace ya
