target("spirv-cross")
do
    set_kind("static")

    add_includedirs("include", "include/spirv_cross", "./")
    add_headerfiles("include/spirv_cross*.h", "include/spirv_cross*.hpp")

    add_includedirs(".")
    add_headerfiles("*.hpp")
    add_files("*.cpp")
end
