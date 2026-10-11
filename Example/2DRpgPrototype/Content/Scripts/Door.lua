-- A doorway: the player steps onto its cell and the map switches (rpg R3).
-- The destination is authored per door through property overrides in the
-- scene (Inspector-editable): targetScene = the scene path, spawnName = the
-- spawn marker inside that scene to land on, actorName = the entity there
-- to place on that marker.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

-- Default matches Town, House, and TownLarge, so those scenes need no override.
Script.actorName = "Player"
Script:properties({
    actorName = {
        type = "string",
        tooltip = "Entity in the destination scene to place on the spawn marker"
    },
})

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
    world.loadScene(self.targetScene, self.spawnName or "", self.actorName or "")
end

return Script
