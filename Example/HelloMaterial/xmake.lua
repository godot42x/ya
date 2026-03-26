target("HelloMaterial")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    add_deps("ya")
    add_rules("c++.unity_build", { batchsize = -1 })

    if is_plat("windows") then
        add_cxxflags("/bigobj")
    end
end
