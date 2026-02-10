#include "UnlitMaterial.h"

namespace ya

{
// resources
TextureView *UnlitMaterial::setTextureView(uint32_t type, const TextureView &tv)
{
    _textureViews[type] = tv;
    setResourceDirty();
    return &_textureViews[type];
}

bool UnlitMaterial::hasTextureView(uint32_t type) const
{
    auto it = _textureViews.find(type);
    return it != _textureViews.end() && it->second.isValid();
}

const TextureView *UnlitMaterial::getTextureView(uint32_t type) const
{
    auto it = _textureViews.find(type);
    if (it != _textureViews.end())
    {
        return &it->second;
    }
    return nullptr;
}

TextureView *UnlitMaterial::getTextureViewMut(uint32_t type)
{
    auto it = _textureViews.find(type);
    if (it != _textureViews.end())
    {
        return &it->second;
    }
    return nullptr;
}

void UnlitMaterial::setTextureViewTexture(uint32_t type, stdptr<Texture> texture)
{
    if (hasTextureView(type)) {
        _textureViews[type].setTexture(texture);
        setResourceDirty();
    }
}

void UnlitMaterial::setTextureViewSampler(uint32_t type, stdptr<Sampler> sampler)
{
    if (hasTextureView(type)) {
        _textureViews[type].setSampler(sampler);
        setResourceDirty();
    }
}


void UnlitMaterial::setTextureViewEnable(uint32_t type, bool benable)
{
    if (hasTextureView(type))
    {
        _textureViews[type].setEnable(benable);
        setParamDirty(true);
    }
}

void UnlitMaterial::setTextureViewUVTranslation(uint32_t type, const glm::vec2 &uvTranslation)
{
    auto *param = getTextureParamMut(type);
    if (param) {
        param->uvTransform.z = uvTranslation.x;
        param->uvTransform.w = uvTranslation.y;
        setParamDirty(true);
    }
}

void UnlitMaterial::setTextureViewUVScale(uint32_t type, const glm::vec2 &uvScale)
{
    auto *param = getTextureParamMut(type);
    if (param) {
        param->uvTransform.x = uvScale.x;
        param->uvTransform.y = uvScale.y;
        setParamDirty(true);
    }
}

void UnlitMaterial::setTextureViewUVRotation(uint32_t type, float uvRotation)
{
    auto *param = getTextureParamMut(type);
    if (param) {
        param->uvRotation = uvRotation;
        setParamDirty(true);
    }
}

void UnlitMaterial::setBaseColor0(const glm::vec3 &baseColor0_)
{
    uMaterial.baseColor0 = baseColor0_;
    setParamDirty();
}

void UnlitMaterial::setBaseColor1(const glm::vec3 &baseColor1_)
{
    uMaterial.baseColor1 = baseColor1_;
    setParamDirty();
}

void UnlitMaterial::setMixValue(float mixValue_)
{
    uMaterial.mixValue = mixValue_;
    setParamDirty();
}


} // namespace ya
