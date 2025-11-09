target("Sandbox")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    -- add_rules("SourceFiles")
    -- if is_plat("windows") and is_kind("shared") then
    --     add_defines("YA_PLATFORM_WINDOWS")
    --     add_defines("YA_BUILD_SHARED")
    -- end


    add_deps("ya")
end


-- YaModule("app", { type = "binary" })
