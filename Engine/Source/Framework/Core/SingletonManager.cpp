#include "SingletonManager.h"
#include <algorithm>
#include <mutex>
#include "Core/Log.h"

namespace ya
{

static std::vector<SingletonManager::Entry> &getRegistry()
{
    static std::vector<SingletonManager::Entry> registry;
    return registry;
}

static std::mutex &getRegistryMutex()
{
    static std::mutex m;
    return m;
}

void SingletonManager::registerSingleton(const std::string &name, int order, std::function<void()> init, std::function<void()> shutdown)
{
    std::lock_guard<std::mutex> lk(getRegistryMutex());
    getRegistry().push_back(Entry{name, order, std::move(init), std::move(shutdown)});
}

void SingletonManager::initAll()
{
    auto &reg = getRegistry();
    std::sort(reg.begin(), reg.end(), [](auto &a, auto &b) { return a.order < b.order; });
    for (auto &e : reg) {
        if (e.init) {
            YA_CORE_INFO("SingletonManager: init {} (order={})", e.name, e.order);
            e.init();
        }
    }
}

void SingletonManager::shutdownAll()
{
    auto &reg = getRegistry();
    std::sort(reg.begin(), reg.end(), [](auto &a, auto &b) { return a.order > b.order; });
    for (auto &e : reg) {
        if (e.shutdown) {
            YA_CORE_INFO("SingletonManager: shutdown {} (order={})", e.name, e.order);
            e.shutdown();
        }
    }
}

} // namespace ya
