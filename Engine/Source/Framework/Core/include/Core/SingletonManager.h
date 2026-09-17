#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>


namespace ya
{

struct SingletonManager
{
    struct Entry
    {
        std::string           name;
        int                   order;
        std::function<void()> init;
        std::function<void()> shutdown;
    };
    // some instance need to be initialized at static init time, we create them by lazy static method in the past
    // now we use this to init them at static init time
    static void initStaticTimeEntries();

    static void registerSingleton(const std::string &name, int order, std::function<void()> init, std::function<void()> shutdown);
    static void initAll();
    static void shutdownAll();
};

// Helper used by translation units to register init/shutdown functions at static init time
struct SingletonRegistrar
{
    SingletonRegistrar(const char *name, int order, std::function<void()> init, std::function<void()> shutdown)
    {
        SingletonManager::registerSingleton(name, order, std::move(init), std::move(shutdown));
    }
};

} // namespace ya
