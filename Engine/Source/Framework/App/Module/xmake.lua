-- Module system: the optional dynamic module/plugin lifecycle (IModule,
-- ModuleManager, manifest/plugin/project descriptors) used by the engine
-- host and app shells that support project/editor plugins. Deliberately NOT
-- part of ya-foundation-core: windowless and GUI-only apps can link the
-- engine closure without pulling in this system.
target("ya-module-manager")
    set_kind(ya_target_kind())
    ya_std_module("YA_MODULE_MANAGER_API")
    add_includedirs("./include", { public = true })
    add_files("*.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_deps("ya-foundation-core", { public = true })
    add_packages("nlohmann_json")
