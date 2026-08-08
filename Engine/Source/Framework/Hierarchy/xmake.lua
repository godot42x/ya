-- Hierarchy: the renderer-independent scene-tree base (Node) shared by the
-- GUI scene (Node2D), the 3D scene (Node3D) and the editor. Kept free of any
-- GUI / Scene3D / ECS type; public API is exposed through the module
-- forwarding root (include/Hierarchy/...).
target("ya-hierarchy")
    set_kind(ya_target_kind())
    ya_std_module("YA_HIERARCHY_API")
    add_includedirs("./include", { public = true })
    add_files("Node.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("Node.h")
    add_deps("ya-foundation-core", { public = true })
