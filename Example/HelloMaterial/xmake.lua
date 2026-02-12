target("HelloMaterial")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    -- add_rules("SourceFiles")
    add_deps("ya")

    if is_plat("windows") then
        add_cxxflags("/bigobj")
    end
end


-- YaModule("app", { type = "binary" })
