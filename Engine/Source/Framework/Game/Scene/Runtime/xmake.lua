-- Scene runtime: SceneManager lifecycle/transitions on top of scene-core.
-- Host registration happens through ISceneLifecycleHost (injected by the
-- Host via Scene::setLifecycleHost), not through App access.
target("ya-scene-runtime")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCENE_RUNTIME_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-scene-core", "ya-scene-serialization", { public = true })
