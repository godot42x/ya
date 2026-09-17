#pragma once

namespace ya
{

template <typename THost>
void CompanionManager::wireImpl(entt::registry& reg, CompanionManager& manager, bool bConnect)
{
    if (bConnect) {
        reg.on_construct<THost>().template connect<&CompanionManager::onHostSignal>(&manager);
        reg.on_update<THost>().template connect<&CompanionManager::onHostSignal>(&manager);
        reg.on_destroy<THost>().template connect<&CompanionManager::onHostSignal>(&manager);
        return;
    }

    reg.on_construct<THost>().template disconnect<&CompanionManager::onHostSignal>(&manager);
    reg.on_update<THost>().template disconnect<&CompanionManager::onHostSignal>(&manager);
    reg.on_destroy<THost>().template disconnect<&CompanionManager::onHostSignal>(&manager);
}

template <typename THost>
void CompanionManager::sweepImpl(entt::registry& reg, CompanionManager& manager)
{
    for (auto [entity, component] : reg.view<THost>().each()) {
        (void)component;
        manager.onHostSignal(reg, entity);
    }
}

} // namespace ya
