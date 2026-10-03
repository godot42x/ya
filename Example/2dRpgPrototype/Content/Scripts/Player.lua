-- Grid walking: one tile per step, eased between tiles, four facings. The
-- frames come from the entity's SpriteAnimationComponent (clips idle_<facing>
-- and walk_<facing> over Textures/hero_walk.png); this script only names the
-- clip that fits the pose.
--
-- The player keeps no grid of its own: its cell is a cell of the
-- TilemapComponent named below, so "where is this tile" (cellToWorld), "may I
-- enter it" (isSolid) and "who stands there" (entityAt) are the map's
-- answers. Painting a wall in the editor, or placing an NPC, blocks the
-- player without touching this script.
local ScriptBase = require("ScriptBase")
local Actor = require("Actor")
local Script = ScriptBase:new()

local STEP_SECONDS = 0.2
local TEXELS_PER_UNIT = 16
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

-- Whole texels, so the nearest-filtered sprite never lands between two.
local function snap(value)
    return math.floor(value * TEXELS_PER_UNIT + 0.5) / TEXELS_PER_UNIT
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
function Script:showPose(bWalking)
    self.anim:play((bWalking and "walk_" or "idle_") .. self.facing.name)
end

-- Put the sprite on its current cell, or between the two cells of a step in
-- progress. The map says where a cell centre is, so a non-unit cell size and
-- a moved tilemap need nothing here. The feet carry the depth: lower on
-- screen draws in front of the actors behind (Actor.zFor).
function Script:applyPosition()
    local from = self.map:cellToWorld(self.cell.x, self.cell.y)
    local x, y = from.x, from.y
    if self.to then
        local target = self.map:cellToWorld(self.to.x, self.to.y)
        x = from.x + (target.x - from.x) * self.progress
        y = from.y + (target.y - from.y) * self.progress
    end
    self.transform:setPosition(Vec3.new(snap(x), snap(y + self.footLift), Actor.zFor(y)))
end

function Script:tryStep(carry)
    local direction = heldDirection()
    if not direction then
        return false
    end
    self.facing = direction
    local nextX = self.cell.x + direction.dx
    local nextY = self.cell.y + direction.dy
    -- The map is the collision authority: a solid tile (painted wall, tree
    -- canopy) or a cell off the map is never entered. An actor's cell blocks
    -- too -- unless its script answers blocksEntry() == false (doors are
    -- walked through); a missing answer means solid.
    if self.map:isSolid(nextX, nextY) then
        return false
    end
    local occupant = self.map:entityAt(nextX, nextY)
    if occupant ~= nil and occupant:call("blocksEntry") ~= false then
        return false
    end
    self.to = { x = nextX, y = nextY }
    self.progress = carry
    return true
end

-- Stepping onto an event's cell pokes it (doors switch maps this way). The
-- player itself is excluded from the query, so the event behind it is seen.
function Script:pokeArrival()
    local occupant = self.map:entityAt(self.cell.x, self.cell.y, self.entity)
    if occupant then
        occupant:call("onPlayerEnter")
    end
end

-- The actor in the cell we face gets its onInteract; a quiet cell does
-- nothing. The NPC answers by turning toward us, the sign by "being read".
function Script:interact()
    local target = self.map:entityAt(self.cell.x + self.facing.dx, self.cell.y + self.facing.dy)
    if target then
        target:call("onInteract")
    end
end

function Script:onInit()
    self.transform = self.entity:getTransform()
    self.sprite = self.entity:getSprite2D()
    self.anim = self.entity:getSpriteAnimation()
    -- The sprite is centred on the entity and may be taller than a tile; lift
    -- it so the feet stand on the tile's bottom edge.
    self.footLift = (self.sprite.size.y - 1) / 2
    local position = self.transform:getPosition()
    self.map = world.find(MAP_NAME):getTilemap()
    -- Start on whichever cell the authored position lands on.
    local cell = self.map:worldToCell(Vec2.new(position.x, position.y))
    self.cell = { x = cell.x, y = cell.y }
    self.to = nil
    self.facing = DIRECTIONS[2]
    self.progress = 0
    self.talking = false
    self.bConfirmArmed = true
    self:showPose(false)
    self:applyPosition()
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
        self.progress = 0
        self:showPose(false)
        self:applyPosition()
        return
    end

    if not self.to and not self:tryStep(0) then
        self.progress = 0
        self:showPose(false)
        self:applyPosition()
        if self.bConfirmArmed and confirmPressed() then
            self.bConfirmArmed = false
            self:interact()
        end
        return
    end

    self.progress = self.progress + dt / STEP_SECONDS
    if self.progress >= 1 then
        self.cell = self.to
        self.to = nil
        -- Poke the cell we just walked onto before possibly walking on.
        self:pokeArrival()
        -- Keep walking without a stop frame while a direction stays held.
        self:tryStep(self.progress - 1)
    end
    self:applyPosition()

    self:showPose(true)
end

return Script
