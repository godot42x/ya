#include "ComponentLinkageSystem.h"

namespace ya
{
namespace
{
LightBillboardPolicy g_lightBillboardPolicy;
}

void ComponentLinkageSystem::setLightBillboardPolicy(LightBillboardPolicy policy)
{
    g_lightBillboardPolicy = std::move(policy);
}

const LightBillboardPolicy& ComponentLinkageSystem::lightBillboardPolicy()
{
    return g_lightBillboardPolicy;
}

} // namespace ya
