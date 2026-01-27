#include "ReflectionCache.h"

#include "Core/Debug/Instrumentor.h"
#include "reflects-core/lib.h"

#include "utility.cc/ranges.h"
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
    ReflectionCache cache;
    cache.typeIndex = typeIndex;

    if (Class *cls = ClassRegistry::instance().getClass(typeIndex)) {
        cache.classPtr      = cls;
        cache.propertyCount = cls->properties.size();
        // 预填充属性渲染上下文
        for (auto &[propName, prop] : cls->properties) {
            cache.propertyContexts[propName] = PropertyRenderContext::createFrom(prop, propName);
        }
    }
    else if (Enum *e = EnumRegistry::instance().getEnum(typeIndex)) {
        cache.bEnum = true;

        auto                             values = e->getValues();
        std::unordered_map<int64_t, int> valueToPosition;
        std::unordered_map<int, int64_t> positionToValue;
        std::vector<std::string>         names;
        std::string                      imguiComboString;


        for (const auto &[idx, values] : ut::enumerate(values)) {
            const auto &n = values.name;
            const auto &v = values.value;
            names.push_back(n);
            imguiComboString.append(n);
            imguiComboString.push_back('\0');
            valueToPosition[v]                     = static_cast<int>(idx);
            positionToValue[static_cast<int>(idx)] = v;
        }

        cache.enumMisc = ReflectionCache::EnumMisc{
            .enumPtr          = e,
            .valueToPosition  = valueToPosition,
            .positionToValue  = positionToValue,
            .names            = names,
            .imguiComboString = imguiComboString,
        };
    }


    return &(_reflectionCache[typeIndex] = cache);
}

} // namespace ya
