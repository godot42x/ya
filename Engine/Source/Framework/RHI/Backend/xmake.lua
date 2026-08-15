-- Generic backend glue: platform-independent texture decode/upload helpers
-- (Texture::fromMemory/fromData/createCubeMap/...) driven through the RHI
-- interfaces, plus the STB single-header implementation they use. The
-- platform-dispatching factories (IRender::create, IRenderPass::create,
-- IDescriptorSetLayout::create, ...) live in ya-rhi-vulkan because they
-- construct concrete backend types.
target("ya-rhi-backend-common")
    set_kind(ya_target_kind())
    ya_std_module("YA_RHI_BACKEND_API")
    -- The common glue layer's own public root: RHI/Backend/TextureLibrary.h
    -- (BuiltinTextureLibrary) belongs here, not to the Vulkan target that
    -- shares this directory (its ./include root used to leak it).
    add_includedirs("./include", { public = true })
    add_headerfiles("./include/**.h", { public = true })
    -- BuiltinTextureLibrary provides the standard white/black/checkerboard
    -- textures and samplers; it lives with the backend because it builds
    -- them through Texture::fromData (resource-factory driven).
    add_files("Texture.cpp", "TextureLibrary.cpp")
    add_files("STB.cpp", { unity_ignored = true })
    add_deps("ya-rhi", { public = true })
    add_packages("vulkansdk", "stb", "ktx")

-- Vulkan backend: the active development backend (Vulkan + VMA). OpenGL
-- sources stay in-tree under OpenGL/ for historical reference but are not
-- built and never mix with the Vulkan target. The factory entry points that
-- dispatch RHI interface creation to the concrete Vulkan types live here,
-- next to the implementations they construct.
target("ya-rhi-vulkan")
    set_kind(ya_target_kind())
    ya_std_module("YA_RHI_BACKEND_API")
    -- The Vulkan backend's public headers (RHI/Backend/Vulkan/*) belong to
    -- this target, not to the platform-independent backend-common layer.
    add_includedirs("./Vulkan/include", { public = true })
    add_headerfiles("./Vulkan/include/**.h", { public = true })
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
