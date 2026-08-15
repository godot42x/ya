#include "ECS/Systems/Components/TerrainComponent.h"

namespace ya
{

TerrainComponent::TerrainComponent()
{
    setupCallbacks();
}

void TerrainComponent::invalidate(uint64_t rebuildNotBeforeFrame)
{
    ++_authoringVersion;
    _heightMapRef.invalidate();
    _rebuildNotBeforeFrame = rebuildNotBeforeFrame;
}

void TerrainComponent::onPostSerialize()
{
    setupCallbacks();
    invalidate();
}

void TerrainComponent::setupCallbacks()
{
    _heightMapRef.onModified.addLambda(this, [this]() {
        invalidate();
    });
}

} // namespace ya
