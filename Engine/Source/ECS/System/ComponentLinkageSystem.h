#pragma once

#include "Core/Base.h"
#include "Core/System/System.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Scene/Scene.h"

#include "Runtime/App/App.h"
#include "entt/entt.hpp"


namespace ya
{

struct Scene;


struct ComponentLinkageSystem : public ISystem
{
    using Self = ComponentLinkageSystem;
    DelegateHandle handle1;
    DelegateHandle handle2;

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

        auto* sceneManager = app->getSceneManager();
        if (!sceneManager) {
            return;
        }

        Scene* scene = sceneManager->getSceneByRegistry(&reg);
        if (!scene) {
            return;
        }

        app->taskManager.registerFrameTask([scene, entity]() {
            auto* appLocal = App::get();
            if (!appLocal) {
                return;
            }

            auto* sceneManagerLocal = appLocal->getSceneManager();
            if (!sceneManagerLocal || !sceneManagerLocal->isSceneValid(scene)) {
                return;
            }

            applyMaterialComponentLinkage(scene, entity);
        });
    }

    ComponentLinkageSystem()
    {
        handle1 = App::get()->getSceneManager()->onSceneInit.addObject(this, &ComponentLinkageSystem::onSceneInit);
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
            });
    }

    ~ComponentLinkageSystem()
    {
        App::get()->getSceneManager()->onSceneInit.remove(handle1);
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

    void onSceneInit(Scene* scene)
    {
        // NOTICE:
        //  on_construct -> component already created
        //  on_update    -> component already updated
        //  on_destroy   -> component is pending to be destroyed and remove, so any_of still true


#define CALL_BACK &ComponentLinkageSystem::OnMaterialComponentChanged

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
    }
};



} // namespace ya