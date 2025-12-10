target("reflects-core")
do
    set_kind("static")
    add_headerfiles("./src/*.h")
    add_files("./src/*.cpp")
    add_includedirs("./src")
    add_includedirs("./includes", { public = true })
end
