#pragma once

#include "Core/Base.h"
#include "Host/Config/ConfigManager.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "Core/System/System.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/TransformSystem.h"
#include "Render3D/Scene.h"

#include "Host/App.h"
#include "Render3D/SceneManager.h"
#include <algorithm>
#include "entt/entt.hpp"


namespace ya
{

struct Scene;


struct ComponentLinkageSystem : public ISystem
{
    using Self = ComponentLinkageSystem;
    struct LightBillboardConfig
    {
        bool        enabled          = true;
        float       screenSizePixels = 30.0f;
        float       minWorldScale    = 0.0f;
        glm::vec4   tint             = glm::vec4(1.0f);
        std::string texturePath{};
    };

    DelegateHandle handle1;
    DelegateHandle handle2;

    static const LightBillboardConfig& pointLightBillboardConfig()
    {
        static LightBillboardConfig config;
        config.enabled          = ConfigManager::get().getOr<bool>("editor", "lightBillboards.point.enabled", true);
        config.screenSizePixels = ConfigManager::get().getOr<float>("editor", "lightBillboards.point.screenSizePixels", 30.0f);
        config.minWorldScale    = ConfigManager::get().getOr<float>("editor", "lightBillboards.point.minWorldScale", 0.4f);
        config.texturePath      = ConfigManager::get().getOr<std::string>("editor",
                                                                           "lightBillboards.point.texturePath",
                                                                           "Engine:Content/TestTextures/icons8-light-64.png");
        config.tint             = glm::vec4(1.0f, 0.95f, 0.45f, 1.0f);
        return config;
    }

    static const LightBillboardConfig& directionalLightBillboardConfig()
    {
        static LightBillboardConfig config;
        config.enabled          = ConfigManager::get().getOr<bool>("editor", "lightBillboards.directional.enabled", true);
        config.screenSizePixels = ConfigManager::get().getOr<float>("editor", "lightBillboards.directional.screenSizePixels", 42.0f);
        config.minWorldScale    = ConfigManager::get().getOr<float>("editor", "lightBillboards.directional.minWorldScale", 1.0f);
        config.texturePath      = ConfigManager::get().getOr<std::string>("editor",
                                                                           "lightBillboards.directional.texturePath",
                                                                           "Engine:Content/TestTextures/icons8-light-64.png");
        config.tint             = glm::vec4(1.0f, 0.96f, 0.72f, 1.0f);
        return config;
    }

    static void applyBillboardDefaults(BillboardComponent& billboard, const LightBillboardConfig& config)
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

    static BillboardComponent* ensureLightManagedBillboard(Scene* scene, const entt::entity entity)
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

    static bool hasLightSource(entt::registry& reg, const entt::entity entity)
    {
        return reg.all_of<PointLightComponent>(entity) || reg.all_of<DirectionalLightComponent>(entity);
    }

    static glm::vec3 resolveEntityForward(entt::registry& reg, const entt::entity entity, const glm::vec3& fallbackDirection)
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

    static void applyPointLightComponentLinkage(Scene* scene, const entt::entity entity)
    {
        if (!scene) {
            return;
        }

        auto& reg = scene->getRegistry();
        if (!reg.valid(entity) || !reg.all_of<PointLightComponent>(entity)) {
            return;
        }

        BillboardComponent* billboard = ensureLightManagedBillboard(scene, entity);
        if (!billboard) {
            return;
        }

        const auto& light  = reg.get<PointLightComponent>(entity);
        const auto& config = pointLightBillboardConfig();
        applyBillboardDefaults(*billboard, config);
        billboard->tint = glm::vec4(light.color * std::max(light.intensity, 0.2f), 1.0f);
        billboard->worldDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        billboard->invalidate();
    }

    static void applyDirectionalLightComponentLinkage(Scene* scene, const entt::entity entity)
    {
        if (!scene) {
            return;
        }

        auto& reg = scene->getRegistry();
        if (!reg.valid(entity) || !reg.all_of<DirectionalLightComponent>(entity)) {
            return;
        }

        BillboardComponent* billboard = ensureLightManagedBillboard(scene, entity);
        if (!billboard) {
            return;
        }

        const auto& light  = reg.get<DirectionalLightComponent>(entity);
        const auto& config = directionalLightBillboardConfig();
        applyBillboardDefaults(*billboard, config);
        billboard->tint = glm::vec4(light._color * std::max(light.intensity, 0.2f), 1.0f);
        billboard->worldDirection = resolveEntityForward(reg, entity, light._direction);
        billboard->invalidate();
    }

    static void applyLightBillboardLinkage(Scene* scene, const entt::entity entity)
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
            applyPointLightComponentLinkage(scene, entity);
        }

        if (reg.all_of<DirectionalLightComponent>(entity)) {
            applyDirectionalLightComponentLinkage(scene, entity);
        }
    }

    static void scheduleLightBillboardLinkage(entt::registry& reg, const entt::entity entity)
    {
        auto* app = App::get();
        if (!app) {
            return;
        }

        auto* sceneManager = app->getSceneServices().getSceneManager();
        if (!sceneManager) {
            return;
        }

        Scene* scene = sceneManager->getSceneByRegistry(&reg);
        if (!scene) {
            return;
        }

        app->getTaskManager().registerFrameTask([scene, entity]() {
            auto* appLocal = App::get();
            if (!appLocal) {
                return;
            }

            auto* sceneManagerLocal = appLocal->getSceneServices().getSceneManager();
            if (!sceneManagerLocal || !sceneManagerLocal->isSceneValid(scene)) {
                return;
            }

            applyLightBillboardLinkage(scene, entity);
        });
    }

    static bool hasAnyMaterialComponent(entt::registry& reg, const entt::entity entity)
    {
        return reg.any_of<PBRMaterialComponent, PhongMaterialComponent, UnlitMaterialComponent, SimpleMaterialComponent>(entity);
    }

    static void applyMaterialComponentLinkage(Scene* scene, const entt::entity entity)
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

    static void scheduleMaterialComponentLinkage(entt::registry& reg, const entt::entity entity)
    {
        auto* app = App::get();
        if (!app) {
            return;
        }

        auto* sceneManager = app->getSceneServices().getSceneManager();
        if (!sceneManager) {
            return;
        }

        Scene* scene = sceneManager->getSceneByRegistry(&reg);
        if (!scene) {
            return;
        }

        app->getTaskManager().registerFrameTask([scene, entity]() {
            auto* appLocal = App::get();
            if (!appLocal) {
                return;
            }

            auto* sceneManagerLocal = appLocal->getSceneServices().getSceneManager();
            if (!sceneManagerLocal || !sceneManagerLocal->isSceneValid(scene)) {
                return;
            }

            applyMaterialComponentLinkage(scene, entity);
        });
    }

    ComponentLinkageSystem()
    {
        handle1 = App::get()->getSceneServices().getSceneManager()->onSceneInit.addObject(this, &ComponentLinkageSystem::onSceneInit);
        handle2 = SceneBus::get().onComponentRemoved.addLambda(
            this,
            [](entt::registry& reg, const entt::entity entity, ya::type_index_t type) {
                if (type == ya::type_index_v<PBRMaterialComponent> ||
                    type == ya::type_index_v<PhongMaterialComponent> ||
                    type == ya::type_index_v<UnlitMaterialComponent> ||
                    type == ya::type_index_v<SimpleMaterialComponent>)
                {
                    scheduleMaterialComponentLinkage(reg, entity);
                }
                if (type == ya::type_index_v<PointLightComponent> ||
                    type == ya::type_index_v<DirectionalLightComponent>)
                {
                    scheduleLightBillboardLinkage(reg, entity);
                    return;
                }
                if (type == ya::type_index_v<BillboardComponent>) {
                    if (reg.valid(entity) && hasLightSource(reg, entity)) {
                        scheduleLightBillboardLinkage(reg, entity);
                    }
                }
            });
    }

    ~ComponentLinkageSystem()
    {
        App::get()->getSceneServices().getSceneManager()->onSceneInit.remove(handle1);
        SceneBus::get().onComponentRemoved.remove(handle2);
    }

    // TODO: a bunch of rule like gameplay tags? treat the components like ga

    // static void on_construct(entt::registry &reg, const entt::entity entity) { OnMaterialComponentChanged(reg, entity); }
    // static void on_update(entt::registry &reg, const entt::entity entity) { OnMaterialComponentChanged(reg, entity); };
    // static void on_destroy(entt::registry &reg, const entt::entity entity) { OnMaterialComponentChanged(reg, entity); };


    // TODO: not only material components management?
    static void OnMaterialComponentChanged(entt::registry& reg, const entt::entity entity)
    {
        scheduleMaterialComponentLinkage(reg, entity);
    }

    static void OnLightComponentChanged(entt::registry& reg, const entt::entity entity)
    {
        scheduleLightBillboardLinkage(reg, entity);
    }

    static void OnTransformComponentChanged(entt::registry& reg, const entt::entity entity)
    {
        if (hasLightSource(reg, entity)) {
            scheduleLightBillboardLinkage(reg, entity);
        }
    }

    void onSceneInit(Scene* scene)
    {
        // NOTICE:
        //  on_construct -> component already created
        //  on_update    -> component already updated
        //  on_destroy   -> component is pending to be destroyed and remove, so any_of still true


#define CALL_BACK &ComponentLinkageSystem::OnMaterialComponentChanged
#define LIGHT_CALL_BACK &ComponentLinkageSystem::OnLightComponentChanged
#define TRANSFORM_LIGHT_CALL_BACK &ComponentLinkageSystem::OnTransformComponentChanged

        auto& _registry = scene->getRegistry();
        _registry.on_construct<PBRMaterialComponent>().connect<CALL_BACK>();
        _registry.on_construct<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_construct<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_construct<SimpleMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<PBRMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<SimpleMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<PBRMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<SimpleMaterialComponent>().connect<CALL_BACK>();

        _registry.on_construct<PointLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_construct<DirectionalLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_update<PointLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_update<DirectionalLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_destroy<PointLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_destroy<DirectionalLightComponent>().connect<LIGHT_CALL_BACK>();
        _registry.on_update<TransformComponent>().connect<TRANSFORM_LIGHT_CALL_BACK>();

        for (const auto [entity, light] : _registry.view<PointLightComponent>().each()) {
            (void)light;
            applyPointLightComponentLinkage(scene, entity);
        }
        for (const auto [entity, light] : _registry.view<DirectionalLightComponent>().each()) {
            (void)light;
            applyDirectionalLightComponentLinkage(scene, entity);
        }

#undef TRANSFORM_LIGHT_CALL_BACK
#undef LIGHT_CALL_BACK
#undef CALL_BACK
    }
};



} // namespace ya
