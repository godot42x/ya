target("layout.cc")
do
    set_kind("static")
    add_files("./src/**.cpp")
    add_headerfiles("*.h", "*.hpp")
    add_includedirs("./src")
    add_includedirs("./src/include", { public = true })
end
