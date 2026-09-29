-- Grid walking: one tile per step, eased between tiles, four facings with a
-- three-frame walk cycle from Textures/hero_walk.png (16x24 frames).
--
-- The player keeps no grid of its own: its cell is a cell of the
-- TilemapComponent named below, so "where is this tile" (cellToWorld) and
-- "may I enter it" (isSolid) are the map's answers. Painting a wall in the
-- editor therefore blocks the player without touching this script.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local STEP_SECONDS = 0.2
local FRAME_SECONDS = 0.12
local TEXELS_PER_UNIT = 16
local MAP_NAME = "TilemapGround"

-- hero_walk.png: 3 columns (left foot, stand, right foot) x 4 rows.
local SHEET_COLUMNS = 3
local SHEET_ROWS = 4
local STAND_COLUMN = 1
local WALK_COLUMNS = { 0, 1, 2, 1 }

-- Checked in order; the first key held wins.
local DIRECTIONS = {
    { keys = { EKey.Up, EKey.K_W }, dx = 0, dy = 1, row = 3 },
    { keys = { EKey.Down, EKey.K_S }, dx = 0, dy = -1, row = 0 },
    { keys = { EKey.Left, EKey.K_A }, dx = -1, dy = 0, row = 1 },
    { keys = { EKey.Right, EKey.K_D }, dx = 1, dy = 0, row = 2 },
}

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

function Script:showFrame(column)
    local u0 = column / SHEET_COLUMNS
    local v0 = self.facing.row / SHEET_ROWS
    self.sprite.uvRect = Vec4.new(u0, v0, u0 + 1 / SHEET_COLUMNS, v0 + 1 / SHEET_ROWS)
end

-- Put the sprite on its current cell, or between the two cells of a step in
-- progress. The map says where a cell centre is, so a non-unit cell size and a
-- moved tilemap need nothing here.
function Script:applyPosition()
    local from = self.map:cellToWorld(self.cell.x, self.cell.y)
    local x, y = from.x, from.y
    if self.to then
        local target = self.map:cellToWorld(self.to.x, self.to.y)
        x = from.x + (target.x - from.x) * self.progress
        y = from.y + (target.y - from.y) * self.progress
    end
    self.transform:setPosition(Vec3.new(snap(x), snap(y + self.footLift), self.z))
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
    -- canopy) or a cell off the map is never entered. Turning toward the wall
    -- still counts as facing it.
    if self.map:isSolid(nextX, nextY) then
        return false
    end
    self.to = { x = nextX, y = nextY }
    self.progress = carry
    return true
end

function Script:onInit()
    self.transform = self.entity:getTransform()
    self.sprite = self.entity:getSprite2D()
    -- The sprite is centred on the entity and may be taller than a tile; lift
    -- it so the feet stand on the tile's bottom edge.
    self.footLift = (self.sprite.size.y - 1) / 2
    local position = self.transform:getPosition()
    self.z = position.z
    self.map = world.find(MAP_NAME):getTilemap()
    -- Start on whichever cell the authored position lands on.
    local cell = self.map:worldToCell(Vec2.new(position.x, position.y))
    self.cell = { x = cell.x, y = cell.y }
    self.to = nil
    self.facing = DIRECTIONS[2]
    self.progress = 0
    self.walkTime = 0
    self:showFrame(STAND_COLUMN)
    self:applyPosition()
end

function Script:onUpdate(dt)
    if not self.to and not self:tryStep(0) then
        self.progress = 0
        self.walkTime = 0
        self:showFrame(STAND_COLUMN)
        self:applyPosition()
        return
    end

    self.progress = self.progress + dt / STEP_SECONDS
    if self.progress >= 1 then
        self.cell = self.to
        self.to = nil
        -- Keep walking without a stop frame while a direction stays held.
        self:tryStep(self.progress - 1)
    end
    self:applyPosition()

    self.walkTime = self.walkTime + dt
    local frame = math.floor(self.walkTime / FRAME_SECONDS) % #WALK_COLUMNS + 1
    self:showFrame(WALK_COLUMNS[frame])
end

return Script

