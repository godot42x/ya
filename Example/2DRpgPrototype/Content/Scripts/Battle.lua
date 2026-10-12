-- Battle.lua -- the turn controller for one battle scene. It owns no unit
-- list: the board is the roster. It walks the tilemap's cells, asks
-- `map:entityAt(x, y)` who stands there, and reads that unit's own script
-- (`team` / `spd` / `hp` / `displayName`). Turn order is speed, highest first.
--
-- Turn phases:
--   select  arrow keys move a free cursor; reachable cells are highlighted
--   move    the active unit walks its path one cell at a time
--   done    reserved for T3 (action menu: attack / skill / wait)
--
-- T2 scope: reachable-range BFS, the cursor, and the walk. Attacking lands in
-- T3.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.mapName = "TilemapGround"
Script.hudName = "BattleHUD"
Script.highlightTexture = "Content:Textures/cell_highlight.png"
Script:properties({
    mapName = { type = "string", tooltip = "Tilemap entity holding the board" },
    hudName = { type = "string", tooltip = "Game UI entry showing the turn order" },
    highlightTexture = { type = "string", tooltip = "Texture used for reachable-cell markers" },
})

local MOVE_SECONDS = 0.14   -- per cell while walking
local DIRS = {
    { dx = 0, dy = 1 },
    { dx = 0, dy = -1 },
    { dx = -1, dy = 0 },
    { dx = 1, dy = 0 },
}

local function cellKey(x, y)
    return x .. "," .. y
end

-- Collect every unit standing on the board, in a stable order (bottom-left to
-- top-right), so a round is deterministic.
function Script:collectUnits()
    self.units = {}
    local map = self.map
    for y = 0, self.height - 1 do
        for x = 0, self.width - 1 do
            local entity = map:entityAt(x, y)
            if entity then
                local unit = world.scriptOf(entity)
                if unit and unit.spd ~= nil then
                    unit.cell = { x = x, y = y }
                    self.units[#self.units + 1] = unit
                end
            end
        end
    end
    table.sort(self.units, function(a, b)
        if a.spd ~= b.spd then
            return a.spd > b.spd
        end
        return tostring(a.entity:getName()) < tostring(b.entity:getName())
    end)
end

-- Push the roster to the HUD. `ui.get` answers a table (the entry's script
-- self) only once that script has loaded, and a root widget handle before
-- that, so the caller retries each frame until this returns true.
function Script:refreshHud()
    local hud = ui.get(self.hudName)
    if type(hud) ~= "table" or hud.setTurnOrder == nil then
        return false
    end
    hud:setTurnOrder(self.round, self.units, self.index)
    if self.active and hud.showActive then
        hud:showActive(self.active.displayName)
    end
    return true
end

function Script:unitAt(x, y, except)
    local entity = self.map:entityAt(x, y, except)
    if not entity then
        return nil
    end
    return world.scriptOf(entity)
end

-- A cell blocks a step when it is off-map / solid, or another living unit
-- stands on it. The unit's own cell never blocks.
function Script:isBlocked(x, y, unit)
    if self.map:isSolid(x, y) then
        return true
    end
    local other = self:unitAt(x, y, unit.entity)
    return other ~= nil and other:isAlive()
end

-- Breadth-first reachable cells within `unit.move`, respecting blockers.
-- `parent` maps a cell to the cell it came from, so a path can be rebuilt.
function Script:reachable(unit)
    local start = unit.cell
    local move = math.floor(unit.move or 0)
    local dist = { [cellKey(start.x, start.y)] = 0 }
    local parent = {}
    local cells = {}
    local frontier = { start }
    for _ = 1, move do
        local next = {}
        for _, c in ipairs(frontier) do
            for _, dir in ipairs(DIRS) do
                local nx, ny = c.x + dir.dx, c.y + dir.dy
                local k = cellKey(nx, ny)
                if dist[k] == nil and not self:isBlocked(nx, ny, unit) then
                    dist[k] = dist[cellKey(c.x, c.y)] + 1
                    parent[k] = c
                    cells[#cells + 1] = { x = nx, y = ny }
                    next[#next + 1] = { x = nx, y = ny }
                end
            end
        end
        frontier = next
    end
    return cells, dist, parent
end

-- Rebuild the walk path to (x, y) from the BFS parent map.
function Script:pathTo(x, y, parent)
    local path = {}
    local cur = { x = x, y = y }
    while cur do
        table.insert(path, 1, cur)
        cur = parent[cellKey(cur.x, cur.y)]
    end
    table.remove(path, 1)  -- drop the starting cell
    return path
end

-- Highlight markers: a pool of sprites reused across turns. They carry
-- bYSort + sortOrder -1, so they sit above the ground line but under any unit
-- on the same row.
function Script:ensureHighlightPool(count)
    if not self.highlights then
        self.highlights = {}
    end
    while #self.highlights < count do
        -- `size` is a glm::vec2, which crosses only as a Vec2 userdata; a plain
        -- {1.0, 1.0} table is not convertible (spawnSprite options are typed).
        local entity = world.spawnSprite("ReachHighlight",
            { texture = self.highlightTexture, size = Vec2.new(1.0, 1.0) })
        if not entity then
            break
        end
        local sprite = entity:getSprite2D()
        if sprite then
            sprite.bYSort = true
            sprite.sortOrder = -1
            sprite.tint = Vec4.new(1.0, 0.92, 0.5, 1.0)
        end
        self.highlights[#self.highlights + 1] = entity
    end
end

function Script:clearHighlights()
    if not self.highlights then
        return
    end
    for _, entity in ipairs(self.highlights) do
        local sprite = entity:getSprite2D()
        if sprite then
            sprite.bVisible = false
        end
    end
end

-- Paint one highlight per reachable cell, at the cell centre, just above the
-- tilemap plane.
function Script:showReachable(cells)
    self:ensureHighlightPool(#cells)
    for i, cell in ipairs(cells) do
        local entity = self.highlights[i]
        if entity then
            local sprite = entity:getSprite2D()
            local p = self.map:cellToWorld(cell.x, cell.y)
            entity:getTransform():setPosition(Vec3.new(p.x, p.y, 0.02))
            if sprite then
                sprite.bVisible = true
            end
        end
    end
    for i = #cells + 1, #self.highlights do
        local sprite = self.highlights[i]:getSprite2D()
        if sprite then
            sprite.bVisible = false
        end
    end
end

function Script:moveCursor(dx, dy)
    local x = self.cursor.x + dx
    local y = self.cursor.y + dy
    if x < 0 or y < 0 or x >= self.width or y >= self.height then
        return
    end
    self.cursor.x, self.cursor.y = x, y
end

-- Activate the unit at self.index, skipping any that have fallen. Returns
-- false when none are left (a side has been wiped -- T3 resolves that).
function Script:beginTurn()
    local guard = 0
    while self.index <= #self.units and not self.units[self.index]:isAlive() do
        self.index = self.index + 1
        guard = guard + 1
        if guard > #self.units * 2 then
            break
        end
    end
    if self.index > #self.units then
        return false
    end
    local unit = self.units[self.index]
    unit:setActive(true)
    self.active = unit
    self:beginSelect(unit)
    self.bHudDirty = true
    return true
end

function Script:beginSelect(unit)
    self.phase = "select"
    self.cursor = { x = unit.cell.x, y = unit.cell.y }
    self.reachCells, self.reachDist, self.reachParent = self:reachable(unit)
    self:showReachable(self.reachCells)
end

function Script:finishTurn()
    self:clearHighlights()
    if self.active then
        self.active:setActive(false)
        self.active = nil
    end
    self.phase = "done"
    self.index = self.index + 1
    if self.index > #self.units then
        self.index = 1
        self.round = self.round + 1
    end
    self.bHudDirty = true
    self:beginTurn()
end

function Script:onInit()
    self.map = world.find(self.mapName)
    if not self.map then
        log:error("Battle: no tilemap named '" .. tostring(self.mapName) .. "'")
        return
    end
    self.map = self.map:getTilemap()
    self.width = self.map.width
    self.height = self.map.height
    self.round = 1
    self.index = 1
    self.units = {}
    self.active = nil
    self.phase = "done"
    self.cursor = { x = 0, y = 0 }
    self.bArmed = true
    self.bHudDirty = false
    self.bCollected = false
end

function Script:updateSelect(dt)
    if input:isActionPressed("move_up") then
        self:moveCursor(0, 1)
    elseif input:isActionPressed("move_down") then
        self:moveCursor(0, -1)
    elseif input:isActionPressed("move_left") then
        self:moveCursor(-1, 0)
    elseif input:isActionPressed("move_right") then
        self:moveCursor(1, 0)
    end
    if not input:isActionDown("confirm") then
        self.bArmed = true
    end
    if self.bArmed and input:isActionPressed("confirm") then
        self.bArmed = false
        local k = cellKey(self.cursor.x, self.cursor.y)
        if self.reachDist[k] ~= nil and self.reachDist[k] > 0 then
            self.path = self:pathTo(self.cursor.x, self.cursor.y, self.reachParent)
            self.pathIndex = 1
            self.moveProgress = 0
            self.phase = "move"
            self:clearHighlights()
        else
            self:finishTurn()
        end
    end
end

function Script:updateMove(dt)
    local unit = self.active
    local step = self.path[self.pathIndex]
    if not step then
        self.phase = "done"
        unit.cell = { x = self.cursor.x, y = self.cursor.y }
        self:finishTurn()
        return
    end
    self.moveProgress = self.moveProgress + dt / MOVE_SECONDS
    local from = self.map:cellToWorld(unit.cell.x, unit.cell.y)
    local to = self.map:cellToWorld(step.x, step.y)
    -- The unit's own transform is cached by Unit.lua (self.transform); the
    -- native entity accessors do not hang off another script's self table.
    local transform = unit.transform
    local z = transform:getPosition().z
    if self.moveProgress >= 1 then
        unit.cell = { x = step.x, y = step.y }
        self.pathIndex = self.pathIndex + 1
        self.moveProgress = 0
        transform:setPosition(Vec3.new(to.x, to.y, z))
        return
    end
    local t = self.moveProgress
    local x = from.x + (to.x - from.x) * t
    local y = from.y + (to.y - from.y) * t
    transform:setPosition(Vec3.new(x, y, z))
end

function Script:onUpdate(dt)
    if not self.map then
        return
    end
    if not self.bCollected then
        self:collectUnits()
        self.bCollected = true
        if #self.units == 0 then
            log:warn("Battle: no units found on the board")
            return
        end
        self:beginTurn()
        return
    end
    if self.bHudDirty or self.bHudPending then
        self.bHudPending = not self:refreshHud()
        if not self.bHudPending then
            self.bHudDirty = false
        end
    end
    if self.phase == "select" then
        self:updateSelect(dt)
    elseif self.phase == "move" then
        self:updateMove(dt)
    end
end

return Script
