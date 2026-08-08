#include "Foundation/Core/Scripting/ScriptApiAsset.h"

#include "Foundation/Core/Scripting/ScriptApiRegistry.h"
#include "Foundation/Core/System/VirtualFileSystem.h"
#include "Framework/Game/Resource/AssetManager.h"

#include <format>

namespace ya
{

namespace
{

using Json  = ScriptApiRegistry::Json;
using Error = ScriptApiRegistry::Error;

AssetManager& requireAssetManager()
{
    if (VirtualFileSystem::get() == nullptr) {
        throw Error("asset system unavailable: virtual file system is not initialized");
    }
    AssetManager* const manager = AssetManager::get();
    if (manager == nullptr) {
        throw Error("asset system unavailable");
    }
    return *manager;
}

std::string requirePath(const Json& args)
{
    if (!args.contains("path") || !args["path"].is_string()) {
        throw Error("requires string param 'path'");
    }
    return args["path"].get<std::string>();
}

} // namespace

void registerAssetScriptApis(ScriptApiRegistry& registry)
{
    static bool bRegistered = false;
    if (bRegistered) {
        return;
    }
    bRegistered = true;

    registry.registerFunction(
        "asset.get_info",
        "Inspects an asset path: {path, type, resourceVersion, textureLoaded, modelLoaded, meta}.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            AssetManager&     manager = requireAssetManager();
            const std::string path    = requirePath(args);
            const AssetMeta&  meta    = manager.getOrLoadMeta(path);
            return Json{
                {"path", path},
                {"type", meta.type},
                {"resourceVersion", manager.getResourceVersion(path)},
                {"textureLoaded", manager.isTextureLoaded(path)},
                {"modelLoaded", manager.isModelLoaded(path)},
                {"meta", meta.toJson()},
            };
        });

    registry.registerFunction(
        "asset.stats",
        "Returns asset cache statistics: {textureCount, modelCount, textureMemoryEstimate}.",
        Json::object(),
        [](const Json&) -> Json {
            const AssetManager::CacheStats stats = requireAssetManager().getStats();
            return Json{
                {"textureCount", stats.textureCount},
                {"modelCount", stats.modelCount},
                {"textureMemoryEstimate", stats.textureMemoryEstimate},
            };
        });

    registry.registerFunction(
        "asset.reload",
        "Invalidates cached data for an asset and bumps its resource version.",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            AssetManager&     manager = requireAssetManager();
            const std::string path    = requirePath(args);
            manager.invalidate(path);
            return Json{
                {"path", path},
                {"resourceVersion", manager.getResourceVersion(path)},
            };
        });

    registry.registerFunction(
        "asset.unload",
        "Removes a cached asset (GPU-safe deferred release).",
        Json{{"path", {{"type", "string"}}}},
        [](const Json& args) -> Json {
            AssetManager&     manager = requireAssetManager();
            const std::string path    = requirePath(args);
            const AssetMeta&  meta    = manager.getOrLoadMeta(path);
            manager.unload(AssetManager::makeCacheKey(path, meta));
            return Json{{"path", path}};
        });

    registry.registerFunction(
        "asset.collect_unused",
        "Releases cached assets whose only reference is the cache itself.",
        Json::object(),
        [](const Json&) -> Json {
            return Json{{"released", requireAssetManager().collectUnused()}};
        });
}

} // namespace ya
