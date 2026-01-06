
#pragma once

#include "ECS/Component.h"
#include "Render/Material/Material.h"
#include "Render/Model.h"
namespace ya
{


// struct MeshProxy{j}

struct StaticMeshComponent : public IComponent
{
    YA_ECS_COMPONENT(StaticMeshComponent);

  public:
    stdptr<Model>           _mesh = nullptr;
    std::string             _assetPath;
    
    // 运行时使用的材质指针（不序列化）
    std::vector<Material *> _materials;
    
    // 序列化使用的材质资源路径
    std::vector<std::string> _materialPaths;



    StaticMeshComponent()                            = default;
    StaticMeshComponent(const StaticMeshComponent &) = default;

    static void registerReflection()
    {
        Register<StaticMeshComponent>("StaticMeshComponent")
            .property("assetPath", &StaticMeshComponent::_assetPath)
            .property("materialPaths", &StaticMeshComponent::_materialPaths);
        // 注意: _materials 不注册，避免序列化指针
    }
};
} // namespace ya