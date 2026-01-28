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

    _reflectionCache[typeIndex] = cache;
    auto ret                    = &_reflectionCache[typeIndex];

    // cache properties
    if (ret->classPtr) {
        for (auto &[propName, prop] : ret->classPtr->properties) {
            ret->propertyContexts[propName] = PropertyRenderContext::createFrom(ret, prop, propName);
        }
    }

    return ret;
}


// ============================================================================
// PropertyRenderContext Implementation
// ============================================================================

PropertyRenderContext PropertyRenderContext::createFrom(ReflectionCache *owner, const Property &prop, const std::string &propName)
{
    PropertyRenderContext ctx;

    // 检查是否为指针类型
    ctx.owner            = owner;
    ctx.bPointer         = prop.bPointer;
    ctx.pointeeTypeIndex = prop.pointeeTypeIndex;

    // 检查是否为容器（指针类型不检查容器）
    if (!ctx.bPointer) {
        ctx.isContainer = ::ya::reflection::PropertyContainerHelper::isContainer(prop);
        if (ctx.isContainer) {
            ctx.containerAccessor = ::ya::reflection::PropertyContainerHelper::getContainerAccessor(prop);
        }
        else {
            // 提取元数据
            auto metadata = prop.getMetadata();
            if (metadata.hasMeta(ya::reflection::Meta::ManipulateSpec::name)) {
                ctx.manipulateSpec =
                    metadata.get<ya::reflection::Meta::ManipulateSpec>(ya::reflection::Meta::ManipulateSpec::name);
            }
            if (metadata.hasMeta(ya::reflection::Meta::Color)) {
                ctx.bColor = metadata.get<bool>(ya::reflection::Meta::Color);
            }
        }
    }

    // 格式化显示名称（移除前缀 _ 和 m_）
    auto sv = std::string_view(propName);
    if (sv.starts_with("_")) {
        sv.remove_prefix(1);
    }
    if (sv.starts_with("m_")) {
        sv.remove_prefix(2);
    }
    ctx.prettyName = std::string(sv);

    return ctx;
}

} // namespace ya
