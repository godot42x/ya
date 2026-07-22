#include "Core/Module/ModuleManager.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_loadso.h>

#include <algorithm>
#include <format>
#include <unordered_set>

namespace ya
{

struct ModuleManager::Context final : FModuleContext
{
    const ModuleManager& manager;

    explicit Context(const ModuleManager& manager) : manager(manager) {}

    void* queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const override
    {
        return manager.queryInterface(moduleName, interfaceId);
    }
};

struct ModuleManager::LoadedModule
{
    SDL_SharedObject*           handle = nullptr;
    const FYaModuleApi*         api    = nullptr;
    IModule*                    instance = nullptr;
    std::unique_ptr<Context>    context;
};

ModuleManager::ModuleManager() = default;

ModuleManager::~ModuleManager()
{
    stopAll();
    unloadAll();
}

bool ModuleManager::fail(std::string message)
{
    _lastError = std::move(message);
    return false;
}

bool ModuleManager::addManifest(const std::filesystem::path& path)
{
    try {
        return addManifest(FModuleManifest::load(path));
    }
    catch (const std::exception& exception) {
        return fail(exception.what());
    }
}

bool ModuleManager::addManifest(FModuleManifest manifest)
{
    if (_manifests.contains(manifest.name)) {
        return fail("Duplicate module manifest: " + manifest.name);
    }
    _manifests.emplace(manifest.name, std::move(manifest));
    return true;
}

bool ModuleManager::visit(const std::string& name,
                          std::unordered_map<std::string, uint8_t>& states,
                          std::vector<std::string>& stack)
{
    const auto manifestIt = _manifests.find(name);
    if (manifestIt == _manifests.end()) {
        return fail("Required module is not registered: " + name);
    }

    if (states[name] == 2) {
        return true;
    }
    if (states[name] == 1) {
        auto cycleBegin = std::find(stack.begin(), stack.end(), name);
        std::string cycle;
        for (auto it = cycleBegin; it != stack.end(); ++it) {
            cycle += (cycle.empty() ? "" : " -> ") + *it;
        }
        cycle += " -> " + name;
        return fail("Module dependency cycle: " + cycle);
    }

    states[name] = 1;
    stack.push_back(name);
    const auto& manifest = manifestIt->second;
    for (const auto& dependency : manifest.dependencies) {
        const auto dependencyIt = _manifests.find(dependency.name);
        if (dependencyIt == _manifests.end()) {
            if (dependency.required) {
                return fail("Module " + name + " requires missing module " + dependency.name);
            }
            continue;
        }
        if (manifest.kind != EModuleKind::Editor && dependencyIt->second.kind == EModuleKind::Editor) {
            return fail("Non-editor module " + name + " cannot depend on editor module " + dependency.name);
        }
        if (!visit(dependency.name, states, stack)) {
            return false;
        }
    }
    stack.pop_back();
    states[name] = 2;
    _resolvedOrder.push_back(name);
    return true;
}

bool ModuleManager::resolve(const std::vector<std::string>& roots)
{
    _lastError.clear();
    _resolvedOrder.clear();
    std::unordered_map<std::string, uint8_t> states;
    std::vector<std::string>                 stack;
    for (const auto& root : roots) {
        if (!visit(root, states, stack)) {
            _resolvedOrder.clear();
            return false;
        }
    }
    return true;
}

std::filesystem::path ModuleManager::resolveBinaryPath(const FModuleManifest& manifest) const
{
    auto makePlatformPath = [](std::filesystem::path path) {
        if (path.has_extension()) {
            return path;
        }
#if defined(_WIN32)
        path += ".dll";
#elif defined(__APPLE__)
        path = path.parent_path() / ("lib" + path.filename().string() + ".dylib");
#else
        path = path.parent_path() / ("lib" + path.filename().string() + ".so");
#endif
        return path;
    };

    const auto binary = makePlatformPath(manifest.binary);
    const auto besideManifest = manifest.sourcePath.parent_path() / binary;
    if (std::filesystem::exists(besideManifest)) {
        return besideManifest;
    }
    if (const char* basePath = SDL_GetBasePath()) {
        const auto besideExecutable = std::filesystem::path(basePath) / binary;
        if (std::filesystem::exists(besideExecutable)) {
            return besideExecutable;
        }
        const auto modulesPath = std::filesystem::path(basePath) / "Modules" / binary.filename();
        if (std::filesystem::exists(modulesPath)) {
            return modulesPath;
        }
    }
    return besideManifest;
}

bool ModuleManager::loadAll()
{
    for (const auto& name : _resolvedOrder) {
        const auto& manifest  = _manifests.at(name);
        const auto  binaryPath = resolveBinaryPath(manifest);
        SDL_SharedObject* handle = SDL_LoadObject(binaryPath.string().c_str());
        if (!handle) {
            unloadAll();
            return fail(std::format("Failed to load module {} from {}: {}", name, binaryPath.string(), SDL_GetError()));
        }

        auto getApi = reinterpret_cast<FGetModuleApi>(SDL_LoadFunction(handle, "yaGetModuleApi"));
        if (!getApi) {
            unloadAll();
            return fail("Module " + name + " does not export yaGetModuleApi");
        }
        const FYaModuleApi* api = getApi(YA_MODULE_ABI_VERSION);
        if (!api || api->structSize < sizeof(FYaModuleApi) || api->abiVersion != YA_MODULE_ABI_VERSION ||
            !api->toolchain || std::string_view(api->toolchain) != YA_MODULE_TOOLCHAIN ||
            !api->architecture || std::string_view(api->architecture) != YA_MODULE_ARCHITECTURE ||
            !api->buildMode || std::string_view(api->buildMode) != YA_MODULE_BUILD_MODE ||
            api->buildFingerprint != YA_MODULE_BUILD_FINGERPRINT ||
            !api->name || manifest.name != api->name || manifest.kind != api->kind ||
            !api->createModule || !api->destroyModule) {
            unloadAll();
            return fail("Module ABI or manifest mismatch: " + name);
        }

        auto loaded     = std::make_unique<LoadedModule>();
        loaded->handle  = handle;
        loaded->api     = api;
        loaded->instance = api->createModule();
        loaded->context = std::make_unique<Context>(*this);
        if (!loaded->instance || !loaded->instance->onLoad(*loaded->context)) {
            if (loaded->instance) {
                loaded->api->destroyModule(loaded->instance);
            }
            unloadAll();
            return fail("Module onLoad failed: " + name);
        }
        _loaded.emplace(name, std::move(loaded));
    }
    return true;
}

bool ModuleManager::startAll(const FEngineContext& context)
{
    for (const auto& name : _resolvedOrder) {
        if (!_loaded.at(name)->instance->onStart(context)) {
            stopAll();
            return fail("Module onStart failed: " + name);
        }
    }
    _started = true;
    return true;
}

void ModuleManager::stopAll()
{
    if (!_started) {
        return;
    }
    for (auto it = _resolvedOrder.rbegin(); it != _resolvedOrder.rend(); ++it) {
        if (const auto loaded = _loaded.find(*it); loaded != _loaded.end()) {
            loaded->second->instance->onStop();
        }
    }
    _started = false;
}

void ModuleManager::unloadAll()
{
    stopAll();
    for (auto it = _resolvedOrder.rbegin(); it != _resolvedOrder.rend(); ++it) {
        const auto loadedIt = _loaded.find(*it);
        if (loadedIt == _loaded.end()) {
            continue;
        }
        auto& loaded = *loadedIt->second;
        loaded.instance->onUnload();
        loaded.api->destroyModule(loaded.instance);
        // ponytail: keep module images resident until process exit; add owner-based registry cleanup before dlclose.
        _loaded.erase(loadedIt);
    }
}

std::vector<IModule*> ModuleManager::getLoadedModules() const
{
    std::vector<IModule*> modules;
    modules.reserve(_resolvedOrder.size());
    for (const auto& name : _resolvedOrder) {
        if (const auto loaded = _loaded.find(name); loaded != _loaded.end()) {
            modules.push_back(loaded->second->instance);
        }
    }
    return modules;
}

void* ModuleManager::queryInterface(std::string_view moduleName, FInterfaceId interfaceId) const
{
    const auto it = _loaded.find(std::string(moduleName));
    return it == _loaded.end() ? nullptr : it->second->instance->queryInterface(interfaceId);
}

std::vector<void*> ModuleManager::queryInterfaces(FInterfaceId interfaceId) const
{
    std::vector<void*> interfaces;
    for (const auto& name : _resolvedOrder) {
        if (const auto loaded = _loaded.find(name); loaded != _loaded.end()) {
            if (void* interfacePtr = loaded->second->instance->queryInterface(interfaceId)) {
                interfaces.push_back(interfacePtr);
            }
        }
    }
    return interfaces;
}

} // namespace ya
