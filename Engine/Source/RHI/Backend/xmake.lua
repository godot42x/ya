ya_module("ya-rhi-backend", "RHI_BACKEND", {
    deps = { "ya-rhi" },
    packages = {
        "vulkansdk",
        "vulkan-memory-allocator",
        "glad",
        "stb",
        "ktx",
        "cxxopts",
    },
    -- OpenGL backend is kept in-tree for reference but not built (Vulkan is
    -- the active development backend).
    exclude = "OpenGL/**.cpp",
    include_root = "../..",
})
