#include "Core/ResourceRegistry.h"
#include "Core/Log.h"

namespace ya
{

void ResourceRegistry::clearAll()
{
    YA_CORE_INFO("ResourceRegistry: Clearing {} caches...", _caches.size());
    
    for (auto& entry : _caches)
    {
        if (entry.cache)
        {
            YA_CORE_INFO("  Clearing cache: {} (priority: {})", 
                entry.cache->getCacheName(), entry.priority);
            entry.cache->clearCache();
        }
    }
    
    _caches.clear();
    YA_CORE_INFO("ResourceRegistry: All caches cleared");
}

} // namespace ya
