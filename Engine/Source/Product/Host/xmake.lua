ya_module("ya-host", "PRODUCT_HOST", {
    include_root = "../..",
    deps = {
        "ya-render-3d",
        "imgui-local",
        "imguizmo-local",
    },
    packages = {
        "libsdl3",
        "asio",
        "cxxopts",
        "vulkan-memory-allocator",
        "glad",
        "lua",
        "sol2",
        "quickjs-ng",
        "vulkansdk",
        "nlohmann_json",
    },
})
