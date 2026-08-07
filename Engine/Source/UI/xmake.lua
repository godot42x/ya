ya_module("ya-ui", "UI", {
    deps = { "ya-rhi" },
    packages = {
        "freetype",
        "glm",
    },
    -- The UI scene module (ya-ui-scene) lives in its own directory below.
    exclude = "Scene/**.cpp",
})

includes("./Scene/xmake.lua")
