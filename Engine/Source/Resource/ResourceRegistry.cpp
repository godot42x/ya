#include "Resource/ResourceRegistry.h"
#include "Core/Log.h"

namespace ya
{

/**

 * @brief Register a resource cache
 * @param cache Pointer to the cache (must outlive the registry)
 * @param priority Higher values are cleared first (default: 0)
 */
void ResourceRegistry::registerCache(IResourceCache *cache, int priority)
{
    _caches.insert({cache->getCacheName(),
                    {
                        .cache    = cache,
                        .priority = priority,
                    }});
    _deleteQueue.emplace_back(cache->getCacheName(), priority);
    std::ranges::sort(_deleteQueue, [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
}

void ResourceRegistry::clearAll()
{
    for (auto &[name, priority] : _deleteQueue)
    {
        _caches[name].cache->clearCache();
    }

    _deleteQueue.clear();
    _caches.clear();
}

} // namespace ya
