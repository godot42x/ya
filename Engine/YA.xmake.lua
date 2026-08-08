local ya_profile = get_config("ya_profile") or "engine"

includes("./Plugins/Plugins.xmake.lua")
includes("./Shader/Shader.xmake.lua")
includes("./ThirdParty/ThirdParty.xmake.lua")
-- Engine test runner + GUI closure test (engine-only targets are guarded
-- inside Engine/Test/xmake.lua; the closure test must exist in gui profile).
includes("./Test/xmake.lua")
includes("./Source/xmake.lua")
