#include "Core/Module/ModuleManager.h"
#include "Core/Module/PluginDescriptor.h"
#include "Core/Module/ProjectDescriptor.h"
#include "Runtime/Application/App.h"

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

} // namespace

int main(int argc, char** argv)
{
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
            if (std::find(roots.begin(), roots.end(), project->mainModule) == roots.end()) {
                roots.insert(roots.begin(), project->mainModule);
            }
            appDesc.projectRoot = project->sourcePath.parent_path().string();
            if (!appDesc.defaultScenePath && project->defaultScene) {
                appDesc.defaultScenePath = project->defaultScene;
            }
        }

        if (appDesc.bEditor) {
            std::string error;
            if (!addPluginDescriptor(moduleManager, roots, "Engine/Plugins/ya-editor/ya-editor.yaplugin", true, error)) {
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

        {
            ya::App app;
            for (ya::IModule* module : moduleManager.getLoadedModules()) {
                app.addModule(*module);
            }
            if (!moduleManager.startAll({.app = &app})) {
                std::fprintf(stderr, "%s\n", moduleManager.getLastError().c_str());
                return 4;
            }
            app.init(std::move(appDesc));
            app.run();
            app.quit();
            moduleManager.stopAll();
        }
        moduleManager.unloadAll();
        return 0;
    }
    catch (const std::exception& exception) {
        std::fprintf(stderr, "ya-runtime startup failed: %s\n", exception.what());
        return 1;
    }
}
