target("GreedySnake")
do
    set_kind("shared")
    add_files("Source/*.cpp")
    add_deps("ya-engine")
end


-- YaModule("app", { type = "binary" })
