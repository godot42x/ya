-- A doorway: the player steps onto its cell and the map switches (rpg R3).
-- The destination is authored per door through property overrides in the
-- scene (Inspector-editable): targetScene = the scene path, spawnName = the
-- spawn marker inside that scene to land on, actorName = the entity there
-- to place on that marker.
--
-- The switch is covered by a short fade (R1): fade the screen out, load the
-- scene at the black, and the destination's own overlay fades back in.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

-- Default matches Town, House, and TownLarge, so those scenes need no override.
Script.actorName = "Player"
-- Seconds to fade before switching, and the overlay entry to drive. A zero
-- fade or a missing overlay switches immediately (no transition).
Script.fadeSeconds = 0.35
Script.fadeEntry = "FadeOverlay"
Script:properties({
    actorName = {
        type = "string",
        tooltip = "Entity in the destination scene to place on the spawn marker"
    },
    fadeSeconds = {
        type = "float",
        min = 0, max = 3,
        tooltip = "Seconds to fade to black before switching; 0 switches immediately"
    },
    fadeEntry = {
        type = "string",
        tooltip = "Game UI entry that fades the screen; empty skips the fade"
    },
})

-- Walkable event: the player walks THROUGH a door, so the entry query is
-- answered "no" -- Player.lua blocks only on a missing/true answer.
function Script:blocksEntry()
    return false
end

function Script:onInit()
    self.timer = nil
    self.bArmed = false
end

function Script:onPlayerEnter()
    if self.targetScene == nil or self.targetScene == "" then
        log:warn("Door '" .. self.entity:getName() .. "' has no targetScene override")
        return
    end
    if self.timer then
        return  -- already opening
    end
    local overlay = self.fadeEntry ~= "" and ui.get(self.fadeEntry) or nil
    if self.fadeSeconds > 0 and type(overlay) == "table" and overlay.fadeOut then
        overlay:fadeOut(self.fadeSeconds)
        self.timer = self.fadeSeconds
    else
        self:switchScene()
    end
end

function Script:switchScene()
    world.loadScene(self.targetScene, self.spawnName or "", self.actorName or "")
end

-- The door counts the fade down and switches at the black. It does nothing
-- while idle, so a world script's per-frame onUpdate is cheap here.
function Script:onUpdate(dt)
    if not self.timer then
        return
    end
    self.timer = self.timer - dt
    if self.timer <= 0 then
        self.timer = nil
        self:switchScene()
    end
end

return Script
