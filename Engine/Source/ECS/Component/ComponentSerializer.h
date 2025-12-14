
#pragma once


#include "Core/Base.h"

#include <entt/entt.hpp>

#include "reflects-core/lib.h"



struct ComponentSerializer
{



    void serializeEntity(std::ostream &writer, entt::registry &registry, entt::entity entity)
    {
        struct Test
        {
            int b;
        };
        if (registry.all_of<Test>(entity)) {

            // auto cls = ClassRegistry::instance().getClass("Test");
            // for (auto prop : cls->propertyLink) {
            //     Property *propInst = cls->getProperty(prop);
            //     auto [t]           = registry.get<Test>(entity);

            //     // cls->getPropertyValue<>()

            //     // propInst->getValueAsString("b");
            // }
        }
    }

    void serilizeProp(auto propMeta, auto){}
};