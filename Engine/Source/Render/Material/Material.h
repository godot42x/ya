#pragma once

#include "Core/Base.h"

#include "Render/Core/Texture.h"



namespace ya
{


//  MARK: Material
/**
 * 一个 Material 表示一种材质类型 (如 UnlitMaterial, LitMaterial 等等) 的一个实例 (instance)。
 * Material 包含该材质类型所需的各种资源的引用 (如纹理等)，以及一些标识信息 (如标签、索引等)。
 * Material 实例由 MaterialFactory 进行创建和管理。
 * Material 和 Material Instance 的区别:
 * - Material: 表示一种材质类型的一个实例，包含该材质类型所需的资源引用和标识信息。
 * - Material Instance: 通常指同一种材质类型的不同实例，它们可能共享相同的资源 (如纹理)，但可以有不同的参数设置 (如颜色、混合值等)。
 */
struct Material
{

    // index: 该类型材质的第n个instance, 从0开始, 由MaterialFactory维护
    // TODO: 区分 material 和 material instance, 当前逻辑不够清楚
    // 如果要删除了一个 material instance呢?
    std::string _label         = "MaterialNone";
    int32_t     _instanceIndex = -1;
    uint32_t    _typeID        = 0;

    std::string getLabel() const { return _label; }
    void        setLabel(const std::string &label) { _label = label; }

    [[nodiscard]] int32_t getIndex() const { return _instanceIndex; }
    void                  setIndex(int32_t index) { _instanceIndex = index; }

    [[nodiscard]] uint32_t getTypeID() const { return _typeID; }
    void                   setTypeID(const uint32_t &typeID) { _typeID = typeID; }


    template <typename T>
    T *as()
    {
        static_assert(std::is_base_of<Material, T>::value, "T must be derived from Material");
        return static_cast<T *>(this);
    }
};


} // namespace ya