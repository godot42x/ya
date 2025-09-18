#pragma once

#include "Core/Base.h"

#include "Render/Core/Texture.h"

#include "Platform/Render/Vulkan/VulkanSampler.h"
#include "glm/glm.hpp"

#include "entt/entt.hpp"
#include <functional>


namespace ya
{

namespace material
{

struct TextureParam
{
    bool enable;
    alignas(4) float uvRotation{0.0f};
    // x,y scale, z,w translate
    alignas(16) glm::vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};
};

struct ModelPushConstant
{
    alignas(16) glm::mat4 modelMatrix{1.0f};
    alignas(16) glm::mat3 normalMatrix{1.0f};
};

} // namespace material


struct TextureView
{

    Texture       *texture = nullptr;
    VulkanSampler *sampler = nullptr;

    bool      bEnable = true;
    glm::vec2 uvTranslation{0.f};
    glm::vec2 uvScale{1.0f};
    float     uvRotation = 0.f;


    [[nodiscard]] bool isValid() const
    {
        return texture && sampler;
    }

    Texture       *getTexture() const { return texture; }
    VulkanSampler *getSampler() const { return sampler; }
};

//  MARK: Material


struct Material
{
    std::string                               _label = "MaterialNone";
    std::unordered_map<uint32_t, TextureView> _textures;

    // index: 该类型材质的第n个instance, 从0开始, 由MaterialFactory维护
    // TODO: 区分 material 和 material instance, 当前逻辑不够清楚
    // 如果要删除了一个 material instance呢?
    int32_t  _instanceIndex = -1;
    uint32_t _typeID        = 0;

    bool bParamDirty    = false;
    bool bResourceDirty = false;



    static void updateTextureParamsByTextureView(const TextureView *tv, material::TextureParam &outParam)
    {
        outParam.enable      = tv->bEnable && tv->isValid();
        outParam.uvRotation  = tv->uvRotation;
        outParam.uvTransform = {
            tv->uvScale.x,
            tv->uvScale.y,
            tv->uvTranslation.x,
            tv->uvTranslation.y,
        };
    }

    [[nodiscard]] int32_t getIndex() const { return _instanceIndex; }
    void                  setIndex(int32_t index) { _instanceIndex = index; }
    uint32_t              getTypeID() const { return _typeID; }
    void                  setTypeID(const uint32_t &typeID) { _typeID = typeID; }

    void               setParamDirty(bool bDirty = true) { bParamDirty = bDirty; }
    void               setResourceDirty(bool bDirty = true) { bResourceDirty = bDirty; }
    [[nodiscard]] bool isParamDirty() const { return bParamDirty; }
    [[nodiscard]] bool isResourceDirty() const { return bResourceDirty; }

    [[nodiscard]] bool hasTexture(uint32_t type) const
    {
        auto it = _textures.find(type);
        return it != _textures.end() && it->second.isValid();
    }

    const TextureView *getTextureView(uint32_t type) const;
    void               setTextureView(uint32_t type, Texture *texture, VulkanSampler *sampler);
    void               setTextureViewSampler(uint32_t type, VulkanSampler *sampler)
    {
        if (hasTexture(type)) {
            _textures[type].sampler = sampler;
            setResourceDirty();
        }
    }
    void setTextureViewEnable(uint32_t type, bool benable);
    void setTextureViewUVTranslation(uint32_t type, const glm::vec2 &uvTranslation);
    void setTextureViewUVScale(uint32_t type, const glm::vec2 &uvScale);
    void setTextureViewUVRotation(uint32_t type, float uvRotation);


    std::string getLabel() const { return _label; }
    void        setLabel(const std::string &label) { _label = label; }



  protected:
    Material() = default;
};



namespace detail
{

// template <typename T>
// auto hash() -> uint32_t;

// template <typename T>
// using hash_t = decltype(hash<T>);

// template <hash_t HashFunc>
template <template <typename> typename HashFunctor>
struct MaterialFactoryInternal
{
    static MaterialFactoryInternal *_instance;

    std::unordered_map<uint32_t, std::vector<std::shared_ptr<Material>>> _materials;

  public:
    static void                     init();
    static MaterialFactoryInternal *get() { return _instance; }

    void destroy();


    template <typename T>
        requires std::is_base_of_v<Material, T>
    std::size_t getMaterialSize() const
    {
        uint32_t typeID = getTypeID<T>();

        auto it = _materials.find(typeID);
        if (it != _materials.end())
        {
            return it->second.size();
        }
        return 0;
    }

    template <typename T>
        requires std::is_base_of<Material, T>::value
    T *createMaterial()
    {
        uint32_t typeID = getTypeID<T>();
        auto     mat    = std::make_shared<T>();
        uint32_t index  = 0;

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
        // index: 该类型材质的第n个instance, 从0开始
        static_cast<Material *>(mat.get())->setIndex(static_cast<int32_t>(index));
        static_cast<Material *>(mat.get())->setTypeID(typeID);
        return mat.get();
    }

    template <typename T>
        requires std::is_base_of<Material, T>::value
    T *createMaterial(std::string label)
    {
        auto *mat = createMaterial<T>();
        static_cast<Material *>(mat)->setLabel(label);
        return mat;
    }

    template <typename T>
        requires std::is_base_of<Material, T>::value
    const std::vector<std::shared_ptr<Material>> &getMaterials() const
    {
        return _materials.at(getTypeID<T>());
    }


  private:
    MaterialFactoryInternal() = default;

    template <typename T>
    [[nodiscard]] constexpr uint32_t getTypeID() const
    {
        return HashFunctor<T>::value();
    }
};
} // namespace detail



using MaterialFactory = detail::MaterialFactoryInternal<ya::TypeIndex>;
} // namespace ya