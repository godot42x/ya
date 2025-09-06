#include "Material.h"

namespace ya
{


template <>
MaterialFactory *MaterialFactory::_instance = nullptr;
template <>
void MaterialFactory::init()
{
    YA_CORE_ASSERT(!_instance, "MaterialFactory already initialized!");
    _instance = new MaterialFactory();
    _instance->_materials.clear();
}

template <>
void MaterialFactory::destroy()
{
    _materials.clear();
    delete _instance;
    _instance = nullptr;
}

const TextureView *Material::getTextureView(uint32_t type) const
{
    auto it = _textures.find(type);
    if (it != _textures.end())
    {
        return &it->second;
    }
    return nullptr;
}

void Material::setTextureView(uint32_t type, Texture *texture, VulkanSampler *sampler)
{
    if (hasTexture(type))
    {
        _textures[type].texture = texture;
        _textures[type].sampler = sampler;
    }
    else {
        _textures.insert({
            type,
            TextureView{
                .texture = texture,
                .sampler = sampler,
            },
        });
    }
    setResourceDirty();
}

void Material::setTextureViewEnable(uint32_t type, bool benable)
{
    if (hasTexture(type))
    {
        _textures[type].bEnable = benable;
        setParamDirty(true);
    }
}

void Material::setTextureViewUVTranslation(uint32_t type, const glm::vec2 &uvTranslation)
{
    if (hasTexture(type))
    {
        _textures[type].uvTranslation = uvTranslation;
        setParamDirty(true);
    }
}

void Material::setTextureViewUVScale(uint32_t type, const glm::vec2 &uvScale)
{

    if (hasTexture(type))
    {
        _textures[type].uvScale = uvScale;
        setParamDirty(true);
    }
}

void Material::setTextureViewUVRotation(uint32_t type, float uvRotation)
{
    if (hasTexture(type))
    {
        _textures[type].uvRotation = uvRotation;
        setParamDirty(true);
    }
}


} // namespace ya
