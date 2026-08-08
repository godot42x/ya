#include "Render3D/Adapters/Material/MaterialRenderLinkageRule.h"

#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Gameplay/Linkage/LinkageFramework.h"
#include "Scene/Core/Scene.h"

namespace ya
{

namespace
{

bool hasAnyMaterialComponent(entt::registry& reg, const entt::entity entity)
{
    return reg.any_of<PBRMaterialComponent, PhongMaterialComponent, UnlitMaterialComponent, SimpleMaterialComponent>(entity);
}

void applyMaterialLinkage(Scene* scene, const entt::entity entity)
{
    if (!scene) {
        return;
    }

    auto& reg = scene->getRegistry();
    if (!reg.valid(entity)) {
        return;
    }

    if (hasAnyMaterialComponent(reg, entity)) {
        if (!reg.all_of<RenderComponent>(entity)) {
            scene->addComponent<RenderComponent>(entity);
        }
    }
    else {
        if (reg.all_of<RenderComponent>(entity)) {
            scene->removeComponent<RenderComponent>(entity);
        }
    }
}

} // namespace

MaterialRenderLinkageRule::MaterialRenderLinkageRule(LinkageFramework* framework)
    : _framework(framework)
{
}

void MaterialRenderLinkageRule::onSceneInit(Scene* scene)
{
    if (!scene || !_framework) {
        return;
    }

    auto& registry = scene->getRegistry();

    registry.on_construct<PBRMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_update<PBRMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_destroy<PBRMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_construct<PhongMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_update<PhongMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_destroy<PhongMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_construct<UnlitMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_update<UnlitMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_destroy<UnlitMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_construct<SimpleMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_update<SimpleMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);
    registry.on_destroy<SimpleMaterialComponent>().connect<&MaterialRenderLinkageRule::onMaterialEvent>(this);

    for (const auto [entity, material] : registry.view<PBRMaterialComponent>().each()) {
        (void)material;
        onMaterialEvent(registry, entity);
    }
    for (const auto [entity, material] : registry.view<PhongMaterialComponent>().each()) {
        (void)material;
        onMaterialEvent(registry, entity);
    }
    for (const auto [entity, material] : registry.view<UnlitMaterialComponent>().each()) {
        (void)material;
        onMaterialEvent(registry, entity);
    }
    for (const auto [entity, material] : registry.view<SimpleMaterialComponent>().each()) {
        (void)material;
        onMaterialEvent(registry, entity);
    }
}

void MaterialRenderLinkageRule::onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type)
{
    if (!_framework) {
        return;
    }

    if (type == ya::type_index_v<PBRMaterialComponent> ||
        type == ya::type_index_v<PhongMaterialComponent> ||
        type == ya::type_index_v<UnlitMaterialComponent> ||
        type == ya::type_index_v<SimpleMaterialComponent>)
    {
        onMaterialEvent(reg, entity);
    }
}

void MaterialRenderLinkageRule::onMaterialEvent(entt::registry& reg, entt::entity entity)
{
    if (!_framework) {
        return;
    }
    Scene* scene = _framework->findScene(reg);
    if (!scene) {
        return;
    }
    _framework->scheduleDeferred(scene, [scene, entity]() {
        applyMaterialLinkage(scene, entity);
    });
}

} // namespace ya
