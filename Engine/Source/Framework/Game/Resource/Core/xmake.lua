-- Resource core: asset descriptors, handles, metadata and imported-data
-- contracts. Pure Core dependency (no RHI/GUI/loader); the concrete ref
-- resolvers and caches live in ya-resource-runtime.
target("ya-resource-core")
    set_kind("shared")
    ya_std_module("YA_RESOURCE_CORE_API")
    ya_tier_include("Game")
    add_files("**.cpp")
    add_headerfiles("**.h")
    add_deps("ya-foundation-core", { public = true })
    add_packages("glm", "nlohmann_json", { public = true })
