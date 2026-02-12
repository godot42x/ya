#pragma once

#include "Core/Base.h"
#include "Core/System/System.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "Scene/Scene.h"

#include "Core/App/App.h"
#include "entt/entt.hpp"


namespace ya
{

struct Scene;


struct ComponentLinkageSystem : public ISystem
{
    using Self = ComponentLinkageSystem;
    DelegateHandle handle1;
    DelegateHandle handle2;

    ComponentLinkageSystem()
    {
        handle1 = App::get()->getSceneManager()->onSceneInit.addObject(this, &ComponentLinkageSystem::onSceneInit);
        handle2 = SceneBus::get().onComponentRemoved.addLambda(
            this,
            [](entt::registry& reg, const entt::entity entity, ya::type_index_t type) {
                if (type == ya::type_index_v<PhongMaterialComponent> ||
                    type == ya::type_index_v<UnlitMaterialComponent> ||
                    type == ya::type_index_v<SimpleMaterialComponent>)
                {
                    OnMaterialComponentChanged(reg, entity);
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
        if (reg.any_of<PhongMaterialComponent, UnlitMaterialComponent, UnlitMaterialComponent>(entity))
        {
            if (!reg.all_of<RenderComponent>(entity))
            {
                if (auto* scene = App::get()->getSceneManager()->getSceneByRegistry(&reg)) {
                    scene->addComponent<RenderComponent>(entity);
                }
            }
        }
        else {
            if (reg.all_of<RenderComponent>(entity)) {
                if (auto* scene = App::get()->getSceneManager()->getSceneByRegistry(&reg)) {
                    scene->removeComponent<RenderComponent>(entity);
                }
            }
        }
    }

    void onSceneInit(Scene* scene)
    {
        // NOTICE:
        //  on_construct -> component already created
        //  on_update    -> component already updated
        //  on_destroy   -> component is pending to be destroyed and remove, so any_of still true


#define CALL_BACK &ComponentLinkageSystem::OnMaterialComponentChanged

        auto& _registry = scene->getRegistry();
        _registry.on_construct<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_construct<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_construct<SimpleMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_update<SimpleMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<PhongMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<UnlitMaterialComponent>().connect<CALL_BACK>();
        _registry.on_destroy<SimpleMaterialComponent>().connect<CALL_BACK>();
    }
};



} // namespace ya