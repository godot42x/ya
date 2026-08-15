#pragma once

#include "Core/Api.h"
#include "App/Module/ModuleManifest.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_SharedObject;

namespace ya
{

class ModuleManager
{
  private:
    struct LoadedModule;
    struct Context;

    std::unordered_map<std::string, FModuleManifest>               _manifests;
    std::unordered_map<std::string, std::unique_ptr<LoadedModule>> _loaded;
    std::vector<std::string>                                       _resolvedOrder;
    std::string                                                    _lastError;
    bool                                                           _started = false;

  public:
    YA_MODULE_MANAGER_API ModuleManager();
    YA_MODULE_MANAGER_API ~ModuleManager();

    ModuleManager(const ModuleManager&)            = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    YA_MODULE_MANAGER_API bool addManifest(const std::filesystem::path& path);
    YA_MODULE_MANAGER_API bool addManifest(FModuleManifest manifest);
    YA_MODULE_MANAGER_API bool resolve(const std::vector<std::string>& roots);
    YA_MODULE_MANAGER_API bool loadAll();
    YA_MODULE_MANAGER_API bool startAll(const FEngineContext& context);
    YA_MODULE_MANAGER_API void stopAll();
    YA_MODULE_MANAGER_API void unloadAll();

    [[nodiscard]] YA_MODULE_MANAGER_API std::vector<IModule*>           getLoadedModules() const;
    [[nodiscard]] YA_MODULE_MANAGER_API void*                           queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const;
    [[nodiscard]] YA_MODULE_MANAGER_API std::vector<void*>              queryInterfaces(FInterfaceId interfaceId) const;
    [[nodiscard]] const std::vector<std::string>& getResolvedOrder() const { return _resolvedOrder; }
    [[nodiscard]] const std::string&              getLastError() const { return _lastError; }

  private:

    bool visit(const std::string&                        name,
               std::unordered_map<std::string, uint8_t>& states,
               std::vector<std::string>&                 stack);
    bool fail(std::string message);

    [[nodiscard]] std::filesystem::path resolveBinaryPath(const FModuleManifest& manifest) const;
};

} // namespace ya
