target("ya-runtime")
do
    set_kind("binary")
    add_files("Source/*.cpp")
    add_deps("ya-engine")
end

