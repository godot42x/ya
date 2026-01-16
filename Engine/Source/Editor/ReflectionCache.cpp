#include "ReflectionCache.h"

#include "Core/Debug/Instrumentor.h"
#include "reflects-core/lib.h"

#include <unordered_map>

namespace ya
{

// 全局反射缓存（按 typeIndex 索引）
static std::unordered_map<size_t, ReflectionCache> _reflectionCache;

// ============================================================================
// ReflectionCache Management
// ============================================================================

ReflectionCache *getOrCreateReflectionCache(uint32_t typeIndex)
{
    YA_PROFILE_FUNCTION();
    auto it = _reflectionCache.find(typeIndex);

    if (it != _reflectionCache.end()) {
        if (auto &cache = it->second; cache.isValid(typeIndex)) {
            // TODO: check type properties changed
            return &it->second;
        }
    }

    auto            cls = ClassRegistry::instance().getClass(typeIndex);
    ReflectionCache cache;
    cache.componentClassPtr = cls;
    cache.propertyCount     = cls ? cls->properties.size() : 0;
    cache.typeIndex         = typeIndex;

    // 预填充属性渲染上下文
    if (cls) {
        for (auto &[propName, prop] : cls->properties) {
            cache.propertyContexts[propName] = PropertyRenderContext::createFrom(prop, propName);
        }
    }

    return &(_reflectionCache[typeIndex] = cache);
}

} // namespace ya
