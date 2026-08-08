#pragma once

#include "ECS/Component.h"
namespace ya
{

namespace ERenderingLayer
{
enum T
{

    Opaque = 0, // Opacity
    // SkyBox
    AlphaTest   = 1,
    Translucent = 2, // no depth write
    Additive    = 3,
};

};



struct RenderComponent : public IComponent
{
    YA_REFLECT_BEGIN(RenderComponent)
    YA_REFLECT_FIELD(RenderingLayer)
    YA_REFLECT_END()

    ERenderingLayer::T RenderingLayer = ERenderingLayer::Opaque;
};



}; // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ERenderingLayer::T)
YA_REFLECT_ENUM_VALUE(Opaque)
YA_REFLECT_ENUM_VALUE(AlphaTest)
YA_REFLECT_ENUM_VALUE(Translucent)
YA_REFLECT_ENUM_VALUE(Additive)
YA_REFLECT_ENUM_END()