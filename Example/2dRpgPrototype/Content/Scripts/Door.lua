-- A doorway: the player steps onto its cell and the map switches (rpg R3).
-- The destination is authored per door through property overrides in the
-- scene (Inspector-editable): targetScene = the scene path, spawnName = the
-- spawn marker inside that scene to land on.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

-- Walkable event: the player walks THROUGH a door, so the entry query is
-- answered "no" -- Player.lua blocks only on a missing/true answer.
function Script:blocksEntry()
    return false
end

function Script:onPlayerEnter()
    if self.targetScene == nil or self.targetScene == "" then
        log:warn("Door '" .. self.entity:getName() .. "' has no targetScene override")
        return
    end
    world.loadScene(self.targetScene, self.spawnName or "")
end

return Script
