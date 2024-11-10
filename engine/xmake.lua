includes("plugin")


target("neon")
do
    add_files("src/**.cpp")
    add_packages("vulkansdk")
end
