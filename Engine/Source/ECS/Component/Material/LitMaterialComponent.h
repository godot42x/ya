/**
 * @brief Lit Material Component - Serializable material data
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System
 * - Mesh data is handled separately by MeshComponent
 *
 * Serialization format:
 * @code
 * {
 *   "LitMaterialComponent": {
 *     "_params": { "ambient": [...], "diffuse": [...], "specular": [...], "shininess": 32.0 },
 *     "_textureSlots": {
 *       "0": { "textureRef": { "_path": "diffuse.png" }, ... },
 *       "1": { "textureRef": { "_path": "specular.png" }, ... }
 *     }
 *   }
 * }
 * @endcode
 */
#pragma once

#include "MaterialComponent.h"
#include "Render/Material/LitMaterial.h"

namespace ya
{


/**
 * @brief LitMaterialComponent - Serializable lit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct LitMaterialComponent : public MaterialComponent<LitMaterial>
{
    YA_REFLECT_BEGIN(LitMaterialComponent, MaterialComponent<LitMaterial>)
    YA_REFLECT_FIELD(_textureSlots)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_END()


    bool _bResolved = false;

    TextureSlotMap        _textureSlots;
    LitMaterial::ParamUBO _params;

  public:
    bool resolve() override;

    void invalidate()
    {
        _material = nullptr;
        for (auto &[t, slot] : _textureSlots)
        {
            slot.invalidate();
        }
        _bResolved = false;
    }
    bool isResolved() const { return _bResolved; }

    TextureSlot &getTextureSlot(LitMaterial::EResource resourceEnum)
    {
        return _textureSlots[resourceEnum];
    }
    TextureSlot &setTextureSlot(LitMaterial::EResource resourceEnum, const std::string &path)
    {
        _textureSlots[resourceEnum].textureRef = TextureRef(path);
        invalidate();
        return _textureSlots[resourceEnum];
    }
    const auto &getParams() const
    {
        // TODO: invalidate after modification
        // invalidate();
        return _params;
    }

    auto &getParamsMut()
    {
        return _params;
    }

    void syncParams();
    void syncTextureSlots();
};

// template <typename T>
// struct ModificationProxy
// {
//     T   *obj;
//     bool bDirty = false;

//     Delegate<void()> onDirty;

//     ModificationProxy(T *obj) : obj(obj) {}
//     ~ModificationProxy()
//     {
//         if (bDirty)
//         {
//             onDirty();
//         }
//     }

//     T *operator->()
//     {
//         bDirty = true;
//         return obj;
//     }

//     T *operator/()
//     {
//         bDirty = true;
//         return obj;
//     }
// };


// void test()
// {
//     struct A
//     {
//         int b;

//     } a;

//     ModificationProxy<A> proxy(&a);
//     proxy->b  = 1;
//     proxy + a = 1;
// }

} // namespace ya