
-- add_requires("")
add_requires("vulkansdk", {
    configs = {
        utils = {
            -- "VkLayer_khronos_validation", -- import layer
            is_mode("debug") and "slangd" or "slang"
        }
    }
})

target("ShaderCompiler")
do
    set_kind("binary")
    add_files("Source/**.cpp")

    add_packages("vulkansdk")

end