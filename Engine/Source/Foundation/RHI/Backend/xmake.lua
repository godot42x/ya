-- Generic backend glue: platform-independent texture decode/upload helpers
-- (Texture::fromMemory/fromData/createCubeMap/...) driven through the RHI
-- interfaces, plus the STB single-header implementation they use. The
-- platform-dispatching factories (IRender::create, IRenderPass::create,
-- IDescriptorSetLayout::create, ...) live in ya-rhi-vulkan because they
-- construct concrete backend types.
target("ya-rhi-backend-common")
    set_kind("shared")
    ya_std_module("YA_RHI_BACKEND_API")
    ya_tier_include("Foundation")
    add_files("Texture.cpp")
    add_files("STB.cpp", { unity_ignored = true })
    add_deps("ya-rhi", { public = true })
    add_packages("vulkansdk", "stb", "ktx")

-- Vulkan backend: the active development backend (Vulkan + VMA). OpenGL
-- sources stay in-tree under OpenGL/ for historical reference but are not
-- built and never mix with the Vulkan target. The factory entry points that
-- dispatch RHI interface creation to the concrete Vulkan types live here,
-- next to the implementations they construct.
target("ya-rhi-vulkan")
    set_kind("shared")
    ya_std_module("YA_RHI_BACKEND_API")
    ya_tier_include("Foundation")
    add_files("Render.cpp", "FrameBuffer.cpp", "RenderPass.cpp", "Swapchain.cpp")
    add_files("DescriptorSet.cpp", "Pipeline.cpp")
    add_files("Vulkan/**.cpp|Vulkan/VulkanMemoryAllocator.cpp")
    add_files("Vulkan/VulkanMemoryAllocator.cpp", { unity_ignored = true })
    add_headerfiles("Vulkan/**.h")
    add_deps("ya-rhi", { public = true })
    -- Vulkan glue uses the interface-driven texture helpers (Texture::wrap)
    -- from the common backend layer; the dependency is one-way.
    add_deps("ya-rhi-backend-common")
    add_packages("vulkansdk", "vulkan-memory-allocator", { public = true })
    add_packages("stb")
