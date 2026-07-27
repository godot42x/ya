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

class ENGINE_API ModuleManager
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
    ModuleManager();
    ~ModuleManager();

    ModuleManager(const ModuleManager&)            = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    bool addManifest(const std::filesystem::path& path);
    bool addManifest(FModuleManifest manifest);
    bool resolve(const std::vector<std::string>& roots);
    bool loadAll();
    bool startAll(const FEngineContext& context);
    void stopAll();
    void unloadAll();

    [[nodiscard]] std::vector<IModule*>           getLoadedModules() const;
    [[nodiscard]] void*                           queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const;
    [[nodiscard]] std::vector<void*>              queryInterfaces(FInterfaceId interfaceId) const;
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
