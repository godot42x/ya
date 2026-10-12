-- Battle.lua -- the turn controller for one battle scene. It owns no unit
-- list: the board is the roster. It walks the tilemap's cells, asks
-- `map:entityAt(x, y)` who stands there, and reads that unit's own script
-- (`team` / `spd` / `hp` / `displayName`). Turn order is speed, highest first.
--
-- Turn phases (one active unit at a time):
--   select   arrow keys move a cursor over highlighted reachable cells
--   menu     after moving (or skipping move): attack / skill / wait
--   target   arrow keys move a cursor over cells in attack range
--   move     the active unit walks its path one cell at a time
--   over     one side has been wiped; the result banner is up
--
-- Combat is deterministic (no RNG), so a battle plays the same way twice and an
-- automation run can assert exact HP.
local ScriptBase = require("ScriptBase")
local BattleFx = require("BattleFx")
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

local function cellDistance(a, b)
    return math.abs(a.x - b.x) + math.abs(a.y - b.y)
end

-- Collect every unit standing on the board, in a stable order (bottom-left to
-- top-right), so a round is deterministic.
function Script:collectUnits()
    self.units = {}
    for y = 0, self.height - 1 do
        for x = 0, self.width - 1 do
            local entity = self.map:entityAt(x, y)
            if entity then
                local unit = world.scriptOf(entity)
                if unit and unit.spd ~= nil then
                    unit.cell = { x = x, y = y }
                    unit.battle = self
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

function Script:livingOf(team)
    local out = {}
    for _, unit in ipairs(self.units) do
        if unit:isAlive() and (team == nil or unit.team == team) then
            out[#out + 1] = unit
        end
    end
    return out
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
    if self.phase == "menu" and hud.showMenu then
        local names = { "Attack" }
        if self.active:hasSkill() then
            names[#names + 1] = self.active.skillName
        end
        names[#names + 1] = "Wait"
        hud:showMenu(names, self.menuIndex)
        if hud.showHint then
            hud:showHint("")
        end
    elseif self.phase == "target" then
        if hud.showHint then
            hud:showHint("Choose a target")
        end
    elseif self.phase == "select" then
        if hud.showMenu then
            hud:showMenu({ "Move" }, 1)
        end
        if hud.showHint then
            hud:showHint("Move, then confirm")
        end
    elseif hud.showHint then
        hud:showHint("")
    end
    return true
end

function Script:setHudDirty()
    self.bHudDirty = true
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

function Script:pathTo(x, y, parent)
    local path = {}
    local cur = { x = x, y = y }
    while cur do
        table.insert(path, 1, cur)
        cur = parent[cellKey(cur.x, cur.y)]
    end
    table.remove(path, 1)
    return path
end

-- ── highlight markers ─────────────────────────────────────────────────────

function Script:ensureHighlightPool(count)
    self.highlights = self.highlights or {}
    while #self.highlights < count do
        local entity = world.spawnSprite("ReachHighlight",
            { texture = self.highlightTexture, size = Vec2.new(1.0, 1.0) })
        if not entity then
            break
        end
        local sprite = entity:getSprite2D()
        if sprite then
            sprite.bYSort = true
            sprite.sortOrder = -1
        end
        self.highlights[#self.highlights + 1] = entity
    end
end

function Script:paintCells(cells, color)
    self:ensureHighlightPool(#cells)
    for i, cell in ipairs(cells) do
        local entity = self.highlights[i]
        if entity then
            local sprite = entity:getSprite2D()
            local p = self.map:cellToWorld(cell.x, cell.y)
            entity:getTransform():setPosition(Vec3.new(p.x, p.y, 0.02))
            if sprite then
                sprite.tint = Vec4.new(color[1], color[2], color[3], color[4])
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

function Script:showReachable(cells)
    self:paintCells(cells, { 1.0, 0.92, 0.5, 1.0 })
end

function Script:showTargets(cells)
    self:paintCells(cells, { 1.0, 0.4, 0.35, 1.0 })
end

function Script:clearHighlights()
    if self.highlights then
        self:paintCells({}, { 1.0, 1.0, 1.0, 1.0 })
    end
end

-- ── cursor ────────────────────────────────────────────────────────────────

function Script:moveCursor(dx, dy)
    local x, y = self.cursor.x + dx, self.cursor.y + dy
    if x < 0 or y < 0 or x >= self.width or y >= self.height then
        return
    end
    self.cursor.x, self.cursor.y = x, y
end

function Script:cursorStep()
    if input:isActionPressed("move_up") then
        self:moveCursor(0, 1)
    elseif input:isActionPressed("move_down") then
        self:moveCursor(0, -1)
    elseif input:isActionPressed("move_left") then
        self:moveCursor(-1, 0)
    elseif input:isActionPressed("move_right") then
        self:moveCursor(1, 0)
    end
end

-- Confirm arming: the press that opened this phase must be released before the
-- next confirm counts, so one key press is never read twice.
function Script:confirmEdge()
    if not input:isActionDown("confirm") then
        self.bArmed = true
    end
    if self.bArmed and input:isActionPressed("confirm") then
        self.bArmed = false
        return true
    end
    return false
end

-- ── phases ────────────────────────────────────────────────────────────────

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
        if not self:checkBattleOver() then
            self.round = self.round + 1
            self.index = 1
            self:beginTurn()
        end
        return false
    end
    local unit = self.units[self.index]
    unit:setActive(true)
    self.active = unit
    self:enterSelect(unit)
    self:setHudDirty()
    return true
end

function Script:enterSelect(unit)
    self.phase = "select"
    self.cursor = { x = unit.cell.x, y = unit.cell.y }
    self.reachCells, self.reachDist, self.reachParent = self:reachable(unit)
    self:showReachable(self.reachCells)
    self.bArmed = false
    self:setHudDirty()
end

function Script:enterMenu()
    self.phase = "menu"
    self.menuIndex = 1
    self.menu = { "attack" }
    if self.active:hasSkill() then
        self.menu[#self.menu + 1] = "skill"
    end
    self.menu[#self.menu + 1] = "wait"
    self:clearHighlights()
    self.bArmed = false
    self:setHudDirty()
end

-- Cells in the active unit's reach that hold a living enemy of the opposite
-- team. Attack and skill differ only by reach and power.
function Script:targetCells(reach)
    local unit = self.active
    local cells = {}
    local index = {}
    for _, other in ipairs(self.units) do
        if other:isAlive() and other.team ~= unit.team then
            if cellDistance(unit.cell, other.cell) <= reach then
                cells[#cells + 1] = { x = other.cell.x, y = other.cell.y }
                index[cellKey(other.cell.x, other.cell.y)] = other
            end
        end
    end
    return cells, index
end

function Script:enterTarget(power, reach, bSkill)
    self.phase = "target"
    self.attackPower = power
    self.bSkillAttack = bSkill
    self.targetCellsList, self.targetIndex = self:targetCells(reach)
    if #self.targetCellsList == 0 then
        -- Nothing in reach: back to the menu, nothing spent.
        self:enterMenu()
        return
    end
    self.cursor = { x = self.active.cell.x, y = self.active.cell.y }
    -- Start on the nearest enemy so the first confirm is meaningful without
    -- hunting with the arrows.
    self:snapTargetCursor()
    self:showTargets(self.targetCellsList)
    self.bArmed = false
    self:setHudDirty()
end

-- Step the target cursor onto the nearest enemy in reach (so the first
-- confirm is meaningful without hunting with the arrows).
function Script:snapTargetCursor()
    local best, bestD
    for _, cell in ipairs(self.targetCellsList) do
        local d = cellDistance(self.active.cell, cell)
        if bestD == nil or d < bestD then
            best, bestD = cell, d
        end
    end
    if best then
        self.cursor.x, self.cursor.y = best.x, best.y
    end
end

function Script:resolveHit(target, power, bSkill)
    local unit = self.active
    local damage = unit:damageAgainst(power, target)
    target:takeDamage(damage)
    local pos = target.transform:getPosition()
    self.fx:spawnDamage(pos.x, pos.y + 0.7, damage)
    self.fx:updateBar(target)
    if bSkill then
        unit.mp = math.max(0, (unit.mp or 0) - (unit.skillCost or 0))
    end
end

function Script:checkBattleOver()
    if self.phase == "over" then
        return true
    end
    local players = #self:livingOf("player")
    local enemies = #self:livingOf("enemy")
    if players == 0 or enemies == 0 then
        self.phase = "over"
        self.winner = players > 0 and "player" or "enemy"
        self.outcome = "victory"
        if players == 0 then
            self.outcome = "defeat"
        end
        self:clearHighlights()
        local hud = ui.get(self.hudName)
        if type(hud) == "table" and hud.showOutcome then
            hud:showOutcome(self.outcome)
        end
        -- Publish the result for the overworld to consume on return (M1).
        Persist.battle = Persist.battle or {}
        Persist.battle.lastOutcome = self.outcome
        Persist.battle.lastScene = "BattleRuins1"
        return true
    end
    return false
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
        if self:checkBattleOver() then
            return
        end
        self.index = 1
        self.round = self.round + 1
    end
    if self:checkBattleOver() then
        return
    end
    self:setHudDirty()
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
    self.units = {}
    self.active = nil
    self.highlights = {}
    self.fx = BattleFx.new(self)
    self.phase = "done"
    self.cursor = { x = 0, y = 0 }
    self.round = 1
    self.index = 1
    self.menuIndex = 1
    self.menu = { "attack", "wait" }
    self.bArmed = false
    self.bHudDirty = false
    self.bCollected = false
    self.barsReady = false
end

function Script:updateSelect(dt)
    self:cursorStep()
    if not self:confirmEdge() then
        return
    end
    local k = cellKey(self.cursor.x, self.cursor.y)
    if self.cursor.x == self.active.cell.x and self.cursor.y == self.active.cell.y then
        -- Confirm on the unit's own cell: skip moving.
        self:enterMenu()
    elseif self.reachDist[k] ~= nil and self.reachDist[k] > 0 then
        self.path = self:pathTo(self.cursor.x, self.cursor.y, self.reachParent)
        self.pathIndex = 1
        self.moveProgress = 0
        self.phase = "move"
        self:clearHighlights()
    else
        -- Unreachable cell: treat as "stay here".
        self:enterMenu()
    end
end

function Script:updateMove(dt)
    local unit = self.active
    local step = self.path[self.pathIndex]
    if not step then
        self.phase = "done"
        unit.cell = { x = self.cursor.x, y = self.cursor.y }
        self:enterMenu()
        return
    end
    self.moveProgress = self.moveProgress + dt / MOVE_SECONDS
    local from = self.map:cellToWorld(unit.cell.x, unit.cell.y)
    local to = self.map:cellToWorld(step.x, step.y)
    local transform = unit.transform
    local z = transform:getPosition().z
    if self.moveProgress >= 1 then
        unit.cell = { x = step.x, y = step.y }
        self.pathIndex = self.pathIndex + 1
        self.moveProgress = 0
        transform:setPosition(Vec3.new(to.x, to.y, z))
        self.fx:updateBar(unit)
        return
    end
    local t = self.moveProgress
    transform:setPosition(Vec3.new(from.x + (to.x - from.x) * t,
                                   from.y + (to.y - from.y) * t, z))
    self.fx:updateBar(unit)
end

function Script:updateMenu(dt)
    if input:isActionPressed("move_up") then
        self.menuIndex = self.menuIndex - 1
        if self.menuIndex < 1 then
            self.menuIndex = #self.menu
        end
        self:setHudDirty()
    elseif input:isActionPressed("move_down") then
        self.menuIndex = self.menuIndex + 1
        if self.menuIndex > #self.menu then
            self.menuIndex = 1
        end
        self:setHudDirty()
    end
    if not self:confirmEdge() then
        return
    end
    local choice = self.menu[self.menuIndex]
    if choice == "attack" then
        self:enterTarget(self.active.atk, self.active.range, false)
    elseif choice == "skill" then
        self:enterTarget(self.active.skillPower, self.active.skillRange, true)
    else
        self:finishTurn()
    end
end

function Script:updateTarget(dt)
    self:cursorStep()
    if not self:confirmEdge() then
        return
    end
    local target = self.targetIndex[cellKey(self.cursor.x, self.cursor.y)]
    if target then
        self:resolveHit(target, self.attackPower, self.bSkillAttack)
        self:clearHighlights()
        self:finishTurn()
    else
        -- Confirmed an empty cell: back to the menu.
        self:enterMenu()
    end
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
    self.fx:update(dt)
    -- One HP bar per living unit, attached on the first frame the HUD paints.
    if not self.barsReady then
        local attached = 0
        for _, unit in ipairs(self.units) do
            if self.fx:attachBar(unit) then
                attached = attached + 1
            end
        end
        self.barsReady = attached == #self.units
    end
    if self.bHudDirty or self.bHudPending then
        self.bHudPending = not self:refreshHud()
        if not self.bHudPending then
            self.bHudDirty = false
        end
    end
    if self.phase == "select" then
        self:updateSelect(dt)
        if self.phase == "target" then
            self:snapTargetCursor()
        end
    elseif self.phase == "move" then
        self:updateMove(dt)
    elseif self.phase == "menu" then
        self:updateMenu(dt)
    elseif self.phase == "target" then
        self:updateTarget(dt)
    end
end

return Script
