target("ya-foundation-rhi-backend")
    set_kind("shared")
    ya_std_module("YA_RHI_BACKEND_API")
    ya_tier_include("Foundation")
    -- OpenGL backend stays in-tree for reference but is not built (Vulkan is
    -- the active development backend). VMA/STB single-header implementations
    -- are compiled outside unity batches.
    add_files("**.cpp|OpenGL/**.cpp|Vulkan/VulkanMemoryAllocator.cpp|STB.cpp")
    add_files("Vulkan/VulkanMemoryAllocator.cpp", { unity_ignored = true })
    add_files("STB.cpp", { unity_ignored = true })
    add_headerfiles("**.h")
    add_deps("ya-foundation-rhi", { public = true })
    add_packages("vulkansdk", "vulkan-memory-allocator", { public = true })
    add_packages("glad", "stb", "ktx", "cxxopts")
