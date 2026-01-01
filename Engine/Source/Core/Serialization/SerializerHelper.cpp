#include "SerializerHelper.h"
#include "Core/System/TypeRegistry.h"

namespace ya
{

nlohmann::json SerializerHelper::anyToJson(const std::any &value, uint32_t typeIndex)
{
    return TypeRegistry::get()->anyToJson(value, typeIndex);
}

std::any SerializerHelper::jsonToAny(const nlohmann::json &j, size_t typeHash)
{
    return TypeRegistry::get()->jsonToAny(j, typeHash);
}

} // namespace ya
