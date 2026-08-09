-- Scene serialization: JSON/reflection/resource-reference scene save & load.
-- Its public header only reaches scene-core; importer/ECS/GUI headers are
-- implementation details of the .cpp (private deps).
target("ya-scene-serialization")
    set_kind(ya_target_kind())
    ya_std_module("YA_SCENE_SERIALIZATION_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-scene-core", { public = true })
    add_deps("ya-resource-core", "ya-resource-loader", "ya-resource-runtime")
    add_packages("glm", "nlohmann_json", { public = true })
