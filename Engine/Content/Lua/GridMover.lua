-- Grid walking: one tile per step, eased between tiles, four facings.
-- The walker keeps no grid of its own. Its cell is a cell of the
-- TilemapComponent passed in, so "where is this tile" (cellToWorld), "may I
-- enter it" (isSolid) and "who stands there" (entityAt) are the map's
-- answers. Painting a wall, or placing an actor, blocks the walker without
-- this module knowing any entity name, map name, or key.
--
-- direction, passed in each tick, is {dx, dy, name} or nil. Only those three
-- fields are read. This module never touches an animation component; the
-- caller turns update's return value and facing.name into a clip.

local Class = require("Class")

local GridMover = Class.define("GridMover")

-- Whole texels, so the nearest-filtered sprite never lands between two.
local function snap(value, texelsPerUnit)
    return math.floor(value * texelsPerUnit + 0.5) / texelsPerUnit
end

function GridMover:__init(entity, options)
    if options == nil or options.map == nil then
        error("GridMover.new requires options.map")
    end
    local stepSeconds = 0.2
    if options.stepSeconds ~= nil then
        stepSeconds = options.stepSeconds
    end
    local texelsPerUnit = 16
    if options.texelsPerUnit ~= nil then
        texelsPerUnit = options.texelsPerUnit
    end

    self.entity = entity
    self.map = options.map
    self.stepSeconds = stepSeconds
    self.texelsPerUnit = texelsPerUnit
    self.transform = entity:getTransform()

    local position = self.transform:getPosition()
    -- Start on whichever cell the authored position lands on.
    local cell = self.map:worldToCell(Vec2.new(position.x, position.y))
    self.cell = { x = cell.x, y = cell.y }
    self.to = nil
    self.facing = options.facing or { dx = 0, dy = -1, name = "down" }
    self.progress = 0
    self:applyPosition()
end

-- Put the sprite on its current cell, or between the two cells of a step in
-- progress. The map says where a cell centre is, so a non-unit cell size and
-- a moved tilemap need nothing here. The entity sits on that centre; the
-- sprite pivot puts the feet on the cell's bottom edge. Who covers whom is
-- the sprite's y-sort (the pivot), not this z. z stays the authored depth
-- against 3D geometry.
function GridMover:applyPosition()
    local from = self.map:cellToWorld(self.cell.x, self.cell.y)
    local x, y = from.x, from.y
    if self.to then
        local target = self.map:cellToWorld(self.to.x, self.to.y)
        x = from.x + (target.x - from.x) * self.progress
        y = from.y + (target.y - from.y) * self.progress
    end
    local z = self.transform:getPosition().z
    self.transform:setPosition(Vec3.new(snap(x, self.texelsPerUnit), snap(y, self.texelsPerUnit), z))
end

-- direction nil refuses without turning. A solid tile (or a cell off the
-- map) is never entered. An actor's cell blocks too -- unless its script
-- answers blocksEntry() == false. No script, no method, or any other answer
-- stays solid. Facing updates even when the step is refused, so a held
-- direction turns in place.
function GridMover:tryStep(direction, carry)
    if not direction then
        return false
    end
    self.facing = direction
    local nextX = self.cell.x + direction.dx
    local nextY = self.cell.y + direction.dy
    if self.map:isSolid(nextX, nextY) then
        return false
    end
    local occupant = self.map:entityAt(nextX, nextY)
    if occupant ~= nil then
        local s = world.scriptOf(occupant)
        if not (s ~= nil and s.blocksEntry ~= nil and s:blocksEntry() == false) then
            return false
        end
    end
    self.to = { x = nextX, y = nextY }
    self.progress = carry
    return true
end

-- Stepping onto an event's cell pokes it (doors switch maps this way). The
-- walker itself is excluded from the query, so the event behind it is seen.
function GridMover:pokeArrival()
    local occupant = self.map:entityAt(self.cell.x, self.cell.y, self.entity)
    if occupant then
        local s = world.scriptOf(occupant)
        if s ~= nil and s.onPlayerEnter ~= nil then
            s:onPlayerEnter()
        end
    end
end

-- Dialogue hold: drop the ease back to the cell we left and keep the
-- in-flight destination. The next update resumes that step.
function GridMover:freeze()
    self.progress = 0
    self:applyPosition()
end

-- One tick. direction is the caller's held direction this frame, or nil.
-- Returns true when this frame is a walk: a step is underway, just landed,
-- or chained into the next cell. False when standing, which is when the
-- caller may interact. The caller chooses the clip from this flag.
function GridMover:update(dt, direction)
    if not self.to and not self:tryStep(direction, 0) then
        self.progress = 0
        self:applyPosition()
        return false
    end

    self.progress = self.progress + dt / self.stepSeconds
    if self.progress >= 1 then
        self.cell = self.to
        self.to = nil
        -- Poke the cell we just walked onto before possibly walking on.
        self:pokeArrival()
        -- Keep walking without a stop frame while a direction stays held.
        self:tryStep(direction, self.progress - 1)
    end
    self:applyPosition()
    return true
end

return GridMover
