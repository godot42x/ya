-- Example products. The GUI framework examples (GUIFrameworkSmoke /
-- GUIWorkbench) are standalone GUI binaries that exist in every profile.
-- The 3D project examples link ya-engine, which the gui profile does not
-- build, so they stay behind the profile guard.

includes("./GUIFrameworkSmoke/xmake.lua")
includes("./GUIWorkbench/xmake.lua")

if get_config("ya_profile") ~= "gui" then
    includes("./HelloMaterial/xmake.lua")
    includes("./GreedySnake/xmake.lua")
    -- Index path is 2DRpgPrototype. The other capitalization is the same
    -- directory on a case-insensitive volume, and xmake then compiles
    -- Entry.cpp twice (a fresh checkout fails to link the example).
    includes("./2DRpgPrototype/xmake.lua")
    includes("./PerfLab/xmake.lua")
end
