target("reflects-core")
do
    set_kind("shared")
    add_defines("REFLECTS_CORE_BUILD=1")
    add_defines("BUILD_SHARED_REFLECTS_CORE=1", { public = true })
    add_headerfiles("./src/*.h")
    add_files("./src/*.cpp")
    add_includedirs("./src")
    add_includedirs("./includes", { public = true })
end
