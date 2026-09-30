#include "ECS/Systems/Components/TerrainComponent.h"

namespace ya
{

void TerrainComponent::invalidate(uint64_t rebuildNotBeforeTick)
{
    ++_authoringVersion;
    _rebuildNotBeforeTick = rebuildNotBeforeTick;
}

void TerrainComponent::onPostSerialize()
{
    invalidate();
}

} // namespace ya
