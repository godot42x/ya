target("reflect.cc")
do
    set_kind("headeronly")
    add_headerfiles("*.h", "*.hpp")
    add_includedirs("./src")
    add_includedirs("./src/include", { public = true })
end
