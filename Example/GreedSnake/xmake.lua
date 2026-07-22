target("GreedySnake")
do
    set_kind("shared")
    add_files("Source/*.cpp")
    add_deps("ya-engine")
    add_includedirs("../../Engine/Source", { public = true })
end


-- YaModule("app", { type = "binary" })
