-- Minimal GUI host: a standalone smoke/demo binary for the GUI product
-- line. Links only the GUI closure (Core/RHI/Vulkan backend + the four GUI
-- modules); no ECS/Physics/Resource/RenderGraph/Render3D/Host/Editor.
target("ya-gui-minimal-host")
    set_kind("binary")
    add_files("main.cpp")
    add_deps("ya-gui-framework")
    add_packages("libsdl3")
