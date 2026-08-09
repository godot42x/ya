#include "GUI/Widgets/UITypeRegistry.h"

#include "Core/Log.h"

#include <algorithm>

namespace ya
{

namespace
{

/// Lease deleter: decrements the module's live-instance count when the last
/// reference (the widget itself) dies.
void releaseModuleInstance(UITypeModule* module)
{
    if (module && module->liveInstances > 0) {
        --module->liveInstances;
    }
}

} // namespace

UITypeRegistry& UITypeRegistry::instance()
{
    static UITypeRegistry registry;
    return registry;
}

std::shared_ptr<UITypeModule> UITypeRegistry::beginModule(const std::string& moduleId)
{
    auto it = _modules.find(moduleId);
    if (it != _modules.end()) {
        return it->second;
    }
    auto module = std::make_shared<UITypeModule>(moduleId);
    _modules.emplace(moduleId, module);
    return module;
}

bool UITypeRegistry::endModule(const std::shared_ptr<UITypeModule>& module)
{
    if (!module) {
        YA_CORE_ERROR("UITypeRegistry::endModule: null module");
        return false;
    }
    if (module->liveInstances > 0) {
        YA_CORE_ERROR("UITypeRegistry::endModule: module '{}' has {} live widget instance(s); "
                      "destroy or migrate them before unloading",
                      module->moduleId, module->liveInstances);
        return false;
    }

    // Drop every type bound to this module, then the module itself.
    for (auto it = _types.begin(); it != _types.end();) {
        if (it->second.module.lock() == module) {
            it = _types.erase(it);
        }
        else {
            ++it;
        }
    }
    _modules.erase(module->moduleId);
    return true;
}

void UITypeRegistry::registerType(const UITypeRegisterInfo& info, std::function<UIElementRef()> factory)
{
    if (info.typeId.empty()) {
        YA_CORE_ERROR("UITypeRegistry::registerType: empty typeId");
        return;
    }
    if (!factory) {
        YA_CORE_ERROR("UITypeRegistry::registerType: null factory for '{}'", info.typeId);
        return;
    }
    if (auto it = _types.find(info.typeId); it != _types.end()) {
        YA_CORE_WARN("UITypeRegistry::registerType: replacing existing type '{}'", info.typeId);
    }

    Entry entry;
    entry.info    = info;
    entry.factory = std::move(factory);
    if (info.module) {
        entry.module = info.module;
    }
    _types.insert_or_assign(info.typeId, std::move(entry));
}

void UITypeRegistry::unregisterType(const std::string& typeId)
{
    _types.erase(typeId);
}

UIElementRef UITypeRegistry::createInstance(const std::string& typeId) const
{
    const auto it = _types.find(typeId);
    if (it == _types.end()) {
        YA_CORE_ERROR("UITypeRegistry::createInstance: unknown type '{}'", typeId);
        return nullptr;
    }

    UIElementRef widget = it->second.factory();
    if (!widget) {
        YA_CORE_ERROR("UITypeRegistry::createInstance: factory for '{}' returned null", typeId);
        return nullptr;
    }

    widget->_typeId = typeId;
    if (auto module = it->second.module.lock()) {
        ++module->liveInstances;
        widget->_moduleLease = std::shared_ptr<UITypeModule>(module.get(), releaseModuleInstance);
    }
    return widget;
}

const UITypeRegisterInfo* UITypeRegistry::findType(const std::string& typeId) const
{
    const auto it = _types.find(typeId);
    return it == _types.end() ? nullptr : &it->second.info;
}

std::vector<std::string> UITypeRegistry::getTypeIds() const
{
    std::vector<std::string> ids;
    ids.reserve(_types.size());
    for (const auto& [typeId, entry] : _types) {
        (void)entry;
        ids.push_back(typeId);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace ya
