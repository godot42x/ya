#include "Material.h"

namespace ya
{


struct SimpleMaterial : public Material
{
    enum EColor
    {
        Normal   = 0,
        Texcoord = 1,
    };
    EColor colorType = Normal;
};
} // namespace ya