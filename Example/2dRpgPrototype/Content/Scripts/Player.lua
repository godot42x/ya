-- Reads input actions and talks. Grid walking (one tile per step, eased, four
-- facings, arrival poke) lives in GridMover. This script turns the held
-- action into a direction and, on confirm, asks the faced cell for onInteract.
local ScriptBase = require("ScriptBase")
local GridMover = require("GridMover")
local Script = ScriptBase:new()

-- Scene propertyOverrides. Defaults are the names Town, House, and TownLarge
-- already use, so those scenes need no overrides.
Script.mapName = "TilemapGround"
Script.dialogueName = "Dialogue"
Script:properties({
    mapName = {
        type = "string",
        tooltip = "Tilemap entity this player walks on"
    },
    dialogueName = {
        type = "string",
        tooltip = "Game UI entry polled for dialogue:busy()"
    }
})

-- Checked in order; the first held action wins. Keys are InputMap.json.
local DIRECTIONS = {
    { action = "move_up", dx = 0, dy = 1, name = "up" },
    { action = "move_down", dx = 0, dy = -1, name = "down" },
    { action = "move_left", dx = -1, dy = 0, name = "left" },
    { action = "move_right", dx = 1, dy = 0, name = "right" },
}

local function heldDirection()
    for _, direction in ipairs(DIRECTIONS) do
        if input:isActionDown(direction.action) then
            return direction
        end
    end
    return nil
end

local function confirmPressed()
    -- isActionPressed is the down edge (isActionDown is the held state).
    return input:isActionPressed("confirm")
end

local function confirmHeld()
    return input:isActionDown("confirm")
end

-- Standing or walking, facing where the last step went. play() on the clip
-- that is already running is a no-op, so this is safe to call every tick.
-- bWalking is GridMover:update's return; dialogue and spawn pass false.
function Script:showPose(bWalking)
    self.anim:play((bWalking and "walk_" or "idle_") .. self.mover.facing.name)
end

-- The actor in the cell we face gets its onInteract; a quiet cell, or a
-- script with no such method, does nothing. The NPC answers by turning
-- toward us, the sign by "being read".
function Script:interact()
    local cell = self.mover.cell
    local facing = self.mover.facing
    local target = self.map:entityAt(cell.x + facing.dx, cell.y + facing.dy)
    if target then
        local s = world.scriptOf(target)
        if s ~= nil and s.onInteract ~= nil then
            s:onInteract()
        end
    end
end

function Script:onInit()
    self.anim = self.entity:getSpriteAnimation()
    self.map = world.find(self.mapName):getTilemap()
    self.talking = false
    self.bConfirmArmed = true
    self.mover = GridMover:new(self.entity, {
        map = self.map,
        stepSeconds = 0.2,
        texelsPerUnit = 16,
        facing = DIRECTIONS[2],
    })
    self:showPose(false)
end

function Script:onUpdate(dt)
    -- A dialogue owns the player while it is up (this hold moves to a
    -- game-ui S4 modal entry once that lands). One press must never trigger
    -- twice: the confirm key has to be released again before interacting.
    -- ui.get falls back to the root widget handle for the first frames of a
    -- scene, until the entry's script has loaded. The handle is a script
    -- reference: reading a field it does not have raises, so only a table
    -- (the live self) is asked :busy().
    local dialogue = ui.get(self.dialogueName)
    self.talking = type(dialogue) == "table" and dialogue.busy ~= nil and dialogue:busy()
    if not confirmHeld() then
        self.bConfirmArmed = true
    end

    if self.talking then
        self.mover:freeze()
        self:showPose(false)
        return
    end

    local walking = self.mover:update(dt, heldDirection())
    self:showPose(walking)
    if not walking and self.bConfirmArmed and confirmPressed() then
        self.bConfirmArmed = false
        self:interact()
    end
end

return Script
