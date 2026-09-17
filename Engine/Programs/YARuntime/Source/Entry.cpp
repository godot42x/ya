#include "App/Module/ModuleManager.h"
#include "App/Module/PluginDescriptor.h"
#include "App/Module/ProjectDescriptor.h"
#include "GameRuntime/App.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>

namespace
{

bool addModuleManifest(ya::ModuleManager& manager,
                       std::vector<std::string>& roots,
                       const std::filesystem::path& path,
                       bool enableEditorModules)
{
    auto manifest = ya::FModuleManifest::load(path);
    if (manifest.kind == ya::EModuleKind::Editor && !enableEditorModules) {
        return true;
    }
    roots.push_back(manifest.name);
    return manager.addManifest(std::move(manifest));
}

bool addPluginDescriptor(ya::ModuleManager& manager,
                         std::vector<std::string>& roots,
                         const std::filesystem::path& path,
                         bool enableEditorModules,
                         std::string& error)
{
    try {
        const auto plugin = ya::FPluginDescriptor::load(path);
        for (const auto& modulePath : plugin.modules) {
            if (!addModuleManifest(manager, roots, modulePath, enableEditorModules)) {
                error = manager.getLastError();
                return false;
            }
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool containsRootModule(const std::vector<std::string>& roots, std::string_view moduleName)
{
    return std::find(roots.begin(), roots.end(), moduleName) != roots.end();
}

/// Stops the modules while the App that hosted them is still alive. onStop()
/// addresses the host the module was started with (the editor reads render state
/// to persist its window layout), and the App still holds the module pointers it
/// detaches during its own teardown. ModuleManager's destruction happens after
/// the App, which inverts both; unloading the instances stays where the App can
/// no longer reach them.
struct FModuleTeardownScope
{
    ya::ModuleManager& manager;

    ~FModuleTeardownScope() { manager.stopAll(); }
};

} // namespace

int main(int argc, char** argv)
{
    Logger::init();
    ya::AppDesc appDesc;
    try {
        appDesc.init(argc, argv);

        ya::ModuleManager          moduleManager;
        std::vector<std::string>   roots;
        std::optional<ya::FProjectDescriptor> project;
        if (appDesc.projectPath) {
            project = ya::FProjectDescriptor::load(*appDesc.projectPath);
            for (const auto& manifest : project->modules) {
                if (!addModuleManifest(moduleManager, roots, manifest, appDesc.bEditor)) {
                    std::fprintf(stderr, "%s\n", moduleManager.getLastError().c_str());
                    return 2;
                }
            }
            for (const auto& pluginPath : project->plugins) {
                std::string error;
                if (!addPluginDescriptor(moduleManager, roots, pluginPath, appDesc.bEditor, error)) {
                    std::fprintf(stderr, "%s\n", error.c_str());
                    return 2;
                }
            }
            if (!containsRootModule(roots, project->mainModule)) {
                std::fprintf(stderr,
                             "Project main module is not provided by project modules/plugins: %s\n",
                             project->mainModule.c_str());
                return 2;
            }
            roots.erase(std::remove(roots.begin(), roots.end(), project->mainModule), roots.end());
            roots.insert(roots.begin(), project->mainModule);
            appDesc.projectRoot = project->sourcePath.parent_path().string();
            if (!appDesc.defaultScenePath && project->defaultScene) {
                appDesc.defaultScenePath = project->defaultScene;
            }
        }

        if (appDesc.bEditor) {
            std::string error;
            if (!addPluginDescriptor(moduleManager, roots, "Engine/Plugins/ya-game-editor/ya-game-editor.yaplugin", true, error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 2;
            }
        }

        if (!roots.empty()) {
            if (!moduleManager.resolve(roots) || !moduleManager.loadAll()) {
                std::fprintf(stderr, "%s\n", moduleManager.getLastError().c_str());
                return 3;
            }
        }

        int exitCode = 0;
        {
            ya::App app;
            for (ya::IModule* module : moduleManager.getLoadedModules()) {
                app.addModule(*module);
            }
            if (!moduleManager.startAll({.app = &app})) {
                std::fprintf(stderr, "%s\n", moduleManager.getLastError().c_str());
                return 4;
            }

            const FModuleTeardownScope modulesTeardown{moduleManager};

            app.init(std::move(appDesc));
            // Propagate the loop's result: a refused start (another live
            // instance on this project) has to be a non-zero exit, otherwise a
            // script cannot tell "ran and finished" from "never ran at all".
            exitCode = app.run();
            app.quit();
        }
        moduleManager.unloadAll();
        return exitCode;
    }
    catch (const std::exception& exception) {
        std::fprintf(stderr, "ya-runtime startup failed: %s\n", exception.what());
        return 1;
    }
}
