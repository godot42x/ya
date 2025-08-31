#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "Render/Mesh.h"


namespace ya
{

template <typename MaterialType>
struct MaterialComponent : public IComponent
{
    std::vector<Mesh *>                                       _meshes;
    std::unordered_map<MaterialType *, std::vector<uint32_t>> _material2meshes;

  public:
    void addMesh(Mesh *mesh, MaterialType *material = nullptr)
    {
        if (!mesh) {
            return;
        }
        uint32_t index = _meshes.size();
        _meshes.push_back(mesh);
        if (auto it = _material2meshes.find(material); it != _material2meshes.end()) {
            it->second.push_back(index);
        }
        else {
            _material2meshes.insert({material, {index}});
        }
    }

    auto     getMaterial2MeshIds() { return _material2meshes; }
    uint32_t getMaterialCount() const { return _material2meshes.count(); }
    Mesh    *getMesh(uint32_t index)
    {
        if (index >= _meshes.size()) {
            return nullptr;
        }
        return _meshes[index];
    }
};

} // namespace ya