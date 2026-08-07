ya_module("ya-scene-core", "SCENE_CORE", {
    deps = { "ya-core" },
    packages = { "glm" },
    -- Node3D is a separate module (ya-scene-3d) below.
    exclude = "3D/**.cpp",
})

includes("./3D/xmake.lua")
