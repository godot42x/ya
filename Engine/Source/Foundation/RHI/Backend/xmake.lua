ya_module("ya-foundation-rhi-backend", "FOUNDATION_RHI_BACKEND", {
    include_root = "../../..",
    deps = { "ya-foundation-rhi" },
    packages = {
        "vulkansdk",
        "vulkan-memory-allocator",
        "glad",
        "stb",
        "ktx",
        "cxxopts",
    },
    -- OpenGL backend is kept in-tree for reference but not built (Vulkan is
    -- the active development backend). VMA/STB single-header implementations
    -- live here too, compiled outside unity batches.
    exclude = "OpenGL/**.cpp|Vulkan/VulkanMemoryAllocator.cpp|STB.cpp",
    unity_ignored = {
        "Vulkan/VulkanMemoryAllocator.cpp",
        "STB.cpp",
    },
    include_root = "../..",
})
