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

    // map: material -> mesh index sequence(index array)
    auto     getMaterial2MeshIDs() { return _material2meshes; }
    uint32_t getMaterialCount() const { return static_cast<uint32_t>(_material2meshes.size()); }
    Mesh    *getMesh(uint32_t index)
    {
        if (index >= _meshes.size()) {
            return nullptr;
        }
        return _meshes[index];
    }
};

} // namespace ya