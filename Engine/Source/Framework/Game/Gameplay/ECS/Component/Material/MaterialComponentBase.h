#pragma once

#include "ECS/Component.h"

namespace ya
{

struct Material;

/**
 * @brief Non-template material-component storage + lifetime.
 *
 * Holds the runtime material instance as an opaque pointer (the render module
 * owns the concrete material types). Destruction and release go through the
 * render module's MaterialFactory; the out-of-line implementation keeps the
 * template component header free of Render3D types, so scene/serialization
 * TUs can construct and destroy material components without reaching Render3D.
 */
struct MaterialComponentBase : public IComponent
{
    Material* _material        = nullptr; ///< Pointer to material instance (managed by MaterialFactory)
    bool      _bSharedMaterial = false;   ///< If true, material is shared and should not be destroyed by this component

    virtual ~MaterialComponentBase();

    void releaseMaterial();

    void setSharedMaterialBase(Material* material);
};

} // namespace ya
