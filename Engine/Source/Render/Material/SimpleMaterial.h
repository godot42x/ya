#include "Material.h"

namespace ya
{


struct SimpleMaterial : public Material
{
    // 为 MaterialRefComponent 模板提供统一接口（SimpleMaterial 无参数 UBO）
    struct ParamUBO {};
    
    enum EColor
    {
        Normal   = 0,
        Texcoord = 1,
    };
    EColor colorType = Normal;
};
} // namespace ya