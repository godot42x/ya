#pragma once

#include "Core/Base.h"

#include "Render/Core/Texture.h"

#include "Platform/Render/Vulkan/VulkanSampler.h"
#include "glm/glm.hpp"


namespace ya
{


struct TextureView
{

    Texture       *texture = nullptr;
    VulkanSampler *sampler = nullptr;

    bool      bEnable = true;
    glm::vec2 uvTranslation{0.f};
    glm::vec2 uvScale{1.0f};
    float     uvRotation = 0.f;


    bool isValid() const
    {
        return bEnable && texture && sampler;
    }
};

namespace EMaterial
{

enum T
{

};
}


struct Material
{
    friend struct MaterialFactory;

    std::unordered_map<uint32_t, TextureView> _textures;
    uint32_t                                  _index = 0;
    bool                                      bDirty = false;

    void setIndex(uint32_t index)
    {
        _index = index;
    }

    void               markDirty() { bDirty = true; }
    [[nodiscard]] bool isDirty() const { return bDirty; }

  protected:
    Material() = default;
};


struct MaterialFactory
{
    static MaterialFactory *_instance;

    std::unordered_map<uint32_t, std::vector<std::shared_ptr<Material>>> _materials;


  public:
    static void             init();
    static MaterialFactory *get() { return _instance; }

    void destroy();


    std::size_t getMaterialSize(uint32_t typeID) const
    {
        auto it = _materials.find(typeID);
        if (it != _materials.end())
        {
            return it->second.size();
        }
        return 0;
    }

    template <typename T>
        requires requires(T t) { t.setIndex(std::declval<uint32_t>()); }
    T *createMaterial(uint32_t typeID)
    {
        auto     mat   = std::make_shared<T>();
        uint32_t index = 0;

        auto it = _materials.find(typeID);
        if (it == _materials.end())
        {
            _materials.insert({typeID, {mat}});
        }
        else
        {
            index = it->second.size();
            _materials[typeID].push_back(mat);
        }
        mat->setIndex(index);
        return mat.get();
    }

  private:
    MaterialFactory() = default;
};

} // namespace ya