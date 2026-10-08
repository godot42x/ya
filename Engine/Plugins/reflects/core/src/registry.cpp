#include "registry.h"

#include <algorithm>

namespace refl
{

uint32_t registrationModule()
{
    return ClassRegistry::instance().registrationModule();
}

void notifyRegistryChanged()
{
    ClassRegistry::instance().noteChanged();
}

} // namespace refl

void ClassRegistry::unregisterModule(ModuleId moduleId)
{
    if (moduleId == kBuiltinModule) {
        return;
    }

    bool changed = false;
    for (auto it = _parentLinks.begin(); it != _parentLinks.end();) {
        if (it->moduleId != moduleId) {
            ++it;
            continue;
        }
        if (Class *child = getClass(it->child)) {
            child->removeParent(it->parent);
        }
        if (const auto parent = parentToChildren.find(it->parent); parent != parentToChildren.end()) {
            std::erase(parent->second, it->child);
        }
        it      = _parentLinks.erase(it);
        changed = true;
    }

    std::vector<refl::type_index_t> removedTypes;
    for (auto it = classes.begin(); it != classes.end();) {
        Class *cls = it->second.get();
        if (cls->moduleId != moduleId) {
            changed = cls->dropOwnedBy(moduleId) || changed;
            ++it;
            continue;
        }
        removedTypes.push_back(cls->typeIndex);
        typeIdMap.erase(cls->typeIndex);
        parentToChildren.erase(cls->typeIndex);
        it      = classes.erase(it);
        changed = true;
    }
    for (const refl::type_index_t type : removedTypes) {
        for (auto &[parent, children] : parentToChildren) {
            (void)parent;
            std::erase(children, type);
        }
        for (auto &[name, cls] : classes) {
            (void)name;
            cls->removeParent(type);
        }
        refl::TypeRegistry::instance().unregisterType(type);
    }
    if (changed) {
        noteChanged();
    }
}

ClassRegistry &ClassRegistry::instance()
{
    static ClassRegistry instance;
    return instance;
}

// 全局辅助函数，用于在 class.h 中访问注册表
ClassRegistry& getClassRegistryInstance()
{
    return ClassRegistry::instance();
}

EnumRegistry &EnumRegistry::instance()
{
    static EnumRegistry reg;
    return reg;
}

