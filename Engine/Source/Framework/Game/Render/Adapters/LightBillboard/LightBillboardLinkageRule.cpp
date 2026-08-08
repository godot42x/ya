#include "Render/Adapters/LightBillboard/LightBillboardLinkageRule.h"

#include "Core/Base.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Entity.h"
#include "Gameplay/Linkage/LinkageFramework.h"
#include "Gameplay/Systems/TransformSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <limits>

namespace ya
{

namespace
{
LightBillboardPolicy g_lightBillboardPolicy;
}

LightBillboardLinkageRule::LightBillboardLinkageRule(LinkageFramework* framework)
    : _framework(framework)
{
}

void LightBillboardLinkageRule::setPolicy(LightBillboardPolicy policy)
{
    g_lightBillboardPolicy = std::move(policy);
}

const LightBillboardPolicy& LightBillboardLinkageRule::policy()
{
    return g_lightBillboardPolicy;
}

namespace
{

const LightBillboardConfig& pointConfig()
{
    return LightBillboardLinkageRule::policy().point;
}

const LightBillboardConfig& directionalConfig()
{
    return LightBillboardLinkageRule::policy().directional;
}

void applyBillboardDefaults(BillboardComponent& billboard, const LightBillboardConfig& config)
{
    billboard.bVisible          = config.enabled;
    billboard.screenSizePixels  = config.screenSizePixels;
    billboard.minWorldScale     = config.minWorldScale;
    billboard.tint              = config.tint;
    billboard.bManagedByLight   = true;
    if (!billboard.image.hasPath() && billboard.image.textureRef.getPath() != config.texturePath) {
        billboard.image.textureRef.setPathWithoutNotify(config.texturePath);
    }
    billboard.invalidate();
}

BillboardComponent* ensureLightManagedBillboard(Scene* scene, const entt::entity entity)
{
    if (!scene) {
        return nullptr;
    }

    auto& reg = scene->getRegistry();
    if (!reg.valid(entity)) {
        return nullptr;
    }

    Entity* owner = scene->getEntityByEnttID(entity);
    if (!owner) {
        return nullptr;
    }

    BillboardComponent* billboard = reg.try_get<BillboardComponent>(entity);
    if (!billboard) {
        billboard = owner->addComponent<BillboardComponent>();
    }

    if (!billboard) {
        return nullptr;
    }

    billboard->bManagedByLight = true;
    return billboard;
}

bool hasLightSource(entt::registry& reg, const entt::entity entity)
{
    return reg.all_of<PointLightComponent>(entity) || reg.all_of<DirectionalLightComponent>(entity);
}

glm::vec3 resolveEntityForward(entt::registry& reg, const entt::entity entity, const glm::vec3& fallbackDirection)
{
    if (auto* transform = reg.try_get<TransformComponent>(entity)) {
        TransformSystem::computeWorldMatrix(transform);
        const glm::vec3 forward = transform->getForward();
        if (glm::length2(forward) > std::numeric_limits<float>::epsilon()) {
            return glm::normalize(forward);
        }
    }

    if (glm::length2(fallbackDirection) > std::numeric_limits<float>::epsilon()) {
        return glm::normalize(fallbackDirection);
    }

    return glm::vec3(0.0f, 0.0f, -1.0f);
}

void applyPointLightLinkage(Scene* scene, const entt::entity entity)
{
    auto& reg = scene->getRegistry();
    if (!reg.valid(entity) || !reg.all_of<PointLightComponent>(entity)) {
        return;
    }

    BillboardComponent* billboard = ensureLightManagedBillboard(scene, entity);
    if (!billboard) {
        return;
    }

    const auto& light  = reg.get<PointLightComponent>(entity);
    const auto& config = pointConfig();
    applyBillboardDefaults(*billboard, config);
    billboard->tint          = glm::vec4(light.color * std::max(light.intensity, 0.2f), 1.0f);
    billboard->worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    billboard->invalidate();
}

void applyDirectionalLightLinkage(Scene* scene, const entt::entity entity)
{
    auto& reg = scene->getRegistry();
    if (!reg.valid(entity) || !reg.all_of<DirectionalLightComponent>(entity)) {
        return;
    }

    BillboardComponent* billboard = ensureLightManagedBillboard(scene, entity);
    if (!billboard) {
        return;
    }

    const auto& light  = reg.get<DirectionalLightComponent>(entity);
    const auto& config = directionalConfig();
    applyBillboardDefaults(*billboard, config);
    billboard->tint           = glm::vec4(light._color * std::max(light.intensity, 0.2f), 1.0f);
    billboard->worldDirection = resolveEntityForward(reg, entity, light._direction);
    billboard->invalidate();
}

} // namespace

void LightBillboardLinkageRule::applyLinkage(Scene* scene, const entt::entity entity)
{
    if (!scene) {
        return;
    }

    auto& reg = scene->getRegistry();
    if (!reg.valid(entity)) {
        return;
    }

    if (!hasLightSource(reg, entity)) {
        if (auto* billboard = reg.try_get<BillboardComponent>(entity); billboard && billboard->bManagedByLight) {
            scene->removeComponent<BillboardComponent>(entity);
        }
        return;
    }

    if (reg.all_of<PointLightComponent>(entity)) {
        applyPointLightLinkage(scene, entity);
    }

    if (reg.all_of<DirectionalLightComponent>(entity)) {
        applyDirectionalLightLinkage(scene, entity);
    }
}

void LightBillboardLinkageRule::onSceneInit(Scene* scene)
{
    if (!scene || !_framework) {
        return;
    }

    auto& registry = scene->getRegistry();

    registry.on_construct<PointLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_update<PointLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_destroy<PointLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_construct<DirectionalLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_update<DirectionalLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_destroy<DirectionalLightComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);
    registry.on_update<TransformComponent>().connect<&LightBillboardLinkageRule::onLightEvent>(this);

    for (const auto [entity, light] : registry.view<PointLightComponent>().each()) {
        (void)light;
        onLightEvent(registry, entity);
    }
    for (const auto [entity, light] : registry.view<DirectionalLightComponent>().each()) {
        (void)light;
        onLightEvent(registry, entity);
    }
}

void LightBillboardLinkageRule::onLightEvent(entt::registry& reg, entt::entity entity)
{
    if (!_framework) {
        return;
    }
    Scene* scene = _framework->findScene(reg);
    if (!scene) {
        return;
    }
    _framework->scheduleDeferred(scene, [scene, entity]() {
        LightBillboardLinkageRule::applyLinkage(scene, entity);
    });
}

void LightBillboardLinkageRule::onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type)
{
    if (!_framework) {
        return;
    }

    if (type == ya::type_index_v<PointLightComponent> || type == ya::type_index_v<DirectionalLightComponent>) {
        Scene* scene = _framework->findScene(reg);
        if (scene) {
            _framework->scheduleDeferred(scene, [scene, entity]() {
                LightBillboardLinkageRule::applyLinkage(scene, entity);
            });
        }
        return;
    }

    if (type == ya::type_index_v<BillboardComponent>) {
        if (reg.valid(entity) && hasLightSource(reg, entity)) {
            Scene* scene = _framework->findScene(reg);
            if (scene) {
                _framework->scheduleDeferred(scene, [scene, entity]() {
                    LightBillboardLinkageRule::applyLinkage(scene, entity);
                });
            }
        }
    }
}

} // namespace ya
