target("ya-foundation-core")
    set_kind(ya_target_kind())
    ya_std_module("YA_CORE_API")
    add_includedirs("./include", { public = true })
    add_files("**.cpp")
    add_headerfiles("./include/**.h", { public = true })
    add_headerfiles("**.h")
    add_deps("utility.cc", "log.cc", "reflects-core", { public = true })
    add_packages("asio", "cxxopts", "glm", "nlohmann_json", { public = true })
    add_packages("libsdl3")
    if is_plat("windows") then
        -- GetProcessMemoryInfo (Core/Os/OsMemory.cpp). Modern SDKs export it
        -- from kernel32, older ones only from psapi; linking both is harmless.
        add_syslinks("psapi")
    end
