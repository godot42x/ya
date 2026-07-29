#pragma once

#include "Core/Api.h"
#include "Core/Module/ModuleManifest.h"

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
    ENGINE_API ModuleManager();
    ENGINE_API ~ModuleManager();

    ModuleManager(const ModuleManager&)            = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    ENGINE_API bool addManifest(const std::filesystem::path& path);
    ENGINE_API bool addManifest(FModuleManifest manifest);
    ENGINE_API bool resolve(const std::vector<std::string>& roots);
    ENGINE_API bool loadAll();
    ENGINE_API bool startAll(const FEngineContext& context);
    ENGINE_API void stopAll();
    ENGINE_API void unloadAll();

    [[nodiscard]] ENGINE_API std::vector<IModule*>           getLoadedModules() const;
    [[nodiscard]] ENGINE_API void*                           queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const;
    [[nodiscard]] ENGINE_API std::vector<void*>              queryInterfaces(FInterfaceId interfaceId) const;
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
