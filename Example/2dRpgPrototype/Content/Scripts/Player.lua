-- Reads keys and talks. Grid walking (one tile per step, eased, four
-- facings, arrival poke) lives in GridMover. This script turns the held key
-- into a direction and, on confirm, asks the faced cell for onInteract.
local ScriptBase = require("ScriptBase")
local GridMover = require("GridMover")
local Script = ScriptBase:new()

local MAP_NAME = "TilemapGround"

-- Checked in order; the first key held wins.
local DIRECTIONS = {
    { keys = { EKey.Up, EKey.K_W }, dx = 0, dy = 1, name = "up" },
    { keys = { EKey.Down, EKey.K_S }, dx = 0, dy = -1, name = "down" },
    { keys = { EKey.Left, EKey.K_A }, dx = -1, dy = 0, name = "left" },
    { keys = { EKey.Right, EKey.K_D }, dx = 1, dy = 0, name = "right" },
}

-- Facing something and tapping one of these triggers its onInteract.
local CONFIRM_KEYS = { EKey.Space, EKey.Enter, EKey.K_E }

local function heldDirection()
    for _, direction in ipairs(DIRECTIONS) do
        for _, key in ipairs(direction.keys) do
            if input:isKeyDown(key) then
                return direction
            end
        end
    end
    return nil
end

local function confirmPressed()
    for _, key in ipairs(CONFIRM_KEYS) do
        -- isKeyPressed is the down edge (isKeyDown is the held state).
        if input:isKeyPressed(key) then
            return true
        end
    end
    return false
end

local function confirmHeld()
    for _, key in ipairs(CONFIRM_KEYS) do
        if input:isKeyDown(key) then
            return true
        end
    end
    return false
end

-- Standing or walking, facing where the last step went. play() on the clip
-- that is already running is a no-op, so this is safe to call every tick.
-- bWalking is GridMover:update's return; dialogue and spawn pass false.
function Script:showPose(bWalking)
    self.anim:play((bWalking and "walk_" or "idle_") .. self.mover.facing.name)
end

-- The actor in the cell we face gets its onInteract; a quiet cell does
-- nothing. The NPC answers by turning toward us, the sign by "being read".
function Script:interact()
    local cell = self.mover.cell
    local facing = self.mover.facing
    local target = self.map:entityAt(cell.x + facing.dx, cell.y + facing.dy)
    if target then
        target:call("onInteract")
    end
end

function Script:onInit()
    self.anim = self.entity:getSpriteAnimation()
    self.map = world.find(MAP_NAME):getTilemap()
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
    -- scene, until the entry's script has loaded; only the script has :busy().
    local dialogue = ui.get("Dialogue")
    self.talking = dialogue ~= nil and dialogue.busy ~= nil and dialogue:busy()
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
