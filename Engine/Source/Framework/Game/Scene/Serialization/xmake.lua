-- Scene serialization: JSON/reflection/resource-reference scene save & load.
-- Its public header only reaches scene-core; importer/ECS/GUI headers are
-- implementation details of the .cpp (private deps).
target("ya-scene-serialization")
    set_kind("shared")
    ya_std_module("YA_SCENE_SERIALIZATION_API")
    ya_tier_include("Scene", "Gameplay", "Framework", "Game")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", "ya-scene-core", { public = true })
    add_deps("ya-gui-scene", "ya-resource-core", "ya-resource-loader", "ya-resource-runtime")
    add_packages("glm", "nlohmann_json", { public = true })
