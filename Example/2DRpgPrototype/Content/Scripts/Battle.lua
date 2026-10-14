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
local BattleAI = require("BattleAI")
local Quests = require("Quests")
local Script = ScriptBase:new()

-- The scene this board is authored in. It keys both the overworld handoff
-- (`Persist.battle.lastScene`) and the quest trigger context, so a "visit this
-- scene" objective is judged against the same name the battle publishes.
local BATTLE_SCENE = "BattleRuins1"

Script.mapName = "TilemapGround"
Script.hudName = "BattleHUD"
Script.highlightTexture = "Content:Textures/range_cell.png"
Script.gridTexture = "Content:Textures/grid_cell.png"
-- Where the outcome sends the party (M1). Empty returnScene keeps the battle
-- on screen (used by the standalone battle tests).
Script.returnScene = ""
Script.returnSpawn = ""
Script.returnActor = "Player"
Script.returnDelay = 2.5
-- The drop rolled on a victory (G2). Empty lootTable uses Loot.table; lootIndex
-- pins a specific row (0 or negative = random), which an automation run sets so
-- the drop is deterministic without touching production content.
Script.lootTable = {}
Script.lootIndex = 0
Script:properties({
    mapName = { type = "string", tooltip = "Tilemap entity holding the board" },
    hudName = { type = "string", tooltip = "Game UI entry showing the turn order" },
    highlightTexture = { type = "string", tooltip = "Texture used for reachable-cell markers" },
    gridTexture = { type = "string", tooltip = "Texture used for the permanent cell grid" },
    returnScene = { type = "string", tooltip = "Scene to load when the battle ends; empty stays put" },
    returnSpawn = { type = "string", tooltip = "Spawn marker in the return scene" },
    returnActor = { type = "string", tooltip = "Entity in the return scene to place on the marker" },
    returnDelay = { type = "float", min = 0.0, max = 30.0, tooltip = "Seconds the outcome banner holds" },
    lootIndex = { type = "int", min = 0, max = 99, tooltip = "0 = random drop row; 1..N pins a row" },
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
        hud:showActive(self.active.displayName, self.active)
    end
    -- The action bar belongs to exactly one phase; every other phase clears
    -- it, so a previous unit's menu never lingers. Items carry id/label/
    -- hotkey/enabled so the bar can gray out what the unit cannot do.
    local items
    local hint = ""
    if self.phase == "menu" then
        items = {}
        for _, name in ipairs(self.menu) do
            if name == "attack" then
                items[#items + 1] = { id = "attack", label = "Attack", hotkey = "1", enabled = true }
            elseif name == "skill" then
                items[#items + 1] = { id = "skill", label = self.active.skillName or "Skill",
                                      hotkey = "2", enabled = self.active:canUseSkill() }
            else
                items[#items + 1] = { id = "wait", label = "Wait", hotkey = "3", enabled = true }
            end
        end
    elseif self.phase == "select" then
        items = { { id = "move", label = "Move", hotkey = "1", enabled = false } }
        hint = "Click a highlighted cell to move"
    elseif self.phase == "target" then
        items = { { id = "attack", label = self.bSkillAttack and (self.active.skillName or "Skill") or "Attack",
                    hotkey = "1", enabled = true },
                  { id = "back", label = "Back", hotkey = "Esc", enabled = true } }
        hint = "Click a target, right-click to go back"
    end
    if hud.showMenu then
        if hud.setMenuReceiver and not self.bMenuReceiverWired then
            self.bMenuReceiverWired = true
            hud:setMenuReceiver(self)
        end
        hud:showMenu(items or {}, items and self.menuIndex or 1)
    end
    if hud.showHint then
        hud:showHint(hint)
    end
    return true
end

-- The action bar reports a click here. `id` is one of the item ids this
-- controller pushed ("attack"/"skill"/"wait"/"back").
function Script:onMenuChosen(id)
    if id == "back" and self.phase == "target" then
        self:enterMenu()
        return
    end
    if self.phase ~= "menu" then
        return
    end
    for index, name in ipairs(self.menu) do
        if name == id then
            self.menuIndex = index
            self:setHudDirty()
            self:chooseMenu(index)
            return
        end
    end
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

-- ── mouse ─────────────────────────────────────────────────────────────────
-- The mouse is a first-class input: hover previews the cell, left click
-- confirms, right click cancels one step. `input:getMousePosition()` is a
-- window point; `world.screenToWorld` takes that same space and answers the
-- world point on the z=0 plane the board lives on.

-- True when the pointer has moved since the last call. A pointer that never
-- moved (headless automation, or a player who only uses the keyboard) must not
-- steer the cursor: `getMousePosition` keeps returning its last value, which
-- would silently override every keypress-driven cursor step.
function Script:pointerMoved()
    local pos = input:getMousePosition()
    if not pos then
        return false
    end
    local moved = self.lastPointerPos == nil
        or pos.x ~= self.lastPointerPos.x
        or pos.y ~= self.lastPointerPos.y
    self.lastPointerPos = { x = pos.x, y = pos.y }
    return moved
end

function Script:mouseCell()
    local pos = input:getMousePosition()
    if not pos or pos.x < 0 or pos.y < 0 then
        return nil
    end
    local ok, worldPos = pcall(world.screenToWorld, pos.x, pos.y)
    if not ok or not worldPos then
        if not self.bWarnedPick then
            self.bWarnedPick = true
            log:warn(string.format("Battle: screenToWorld failed (%s) at %.0f,%.0f",
                                   tostring(worldPos), pos.x, pos.y))
        end
        return nil
    end
    local cell = self.map:worldToCell(worldPos)
    local x, y = math.floor(cell.x + 0.5), math.floor(cell.y + 0.5)
    if x < 0 or y < 0 or x >= self.width or y >= self.height then
        if not self.bWarnedOffBoard then
            self.bWarnedOffBoard = true
            log:warn(string.format("Battle: mouse off board: window %.0f,%.0f -> world %.2f,%.2f -> cell %.1f,%.1f (board %dx%d)",
                                   pos.x, pos.y, worldPos.x, worldPos.y, cell.x, cell.y, self.width, self.height))
        end
        return nil
    end
    if not self.bLoggedPick then
        self.bLoggedPick = true
        log:info(string.format("Battle: pick ok: window %.0f,%.0f -> world %.2f,%.2f -> cell %d,%d",
                               pos.x, pos.y, worldPos.x, worldPos.y, x, y))
    end
    return { x = x, y = y }
end

function Script:mouseLeftPressed()
    return input:isMouseButtonPressed(EMouse.Left)
end

function Script:mouseRightPressed()
    return input:isMouseButtonPressed(EMouse.Right)
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
    -- An enemy is played by the AI (no cursor); a player unit waits for input.
    if unit.team == "enemy" then
        self:beginAiTurn(unit)
    else
        self:enterSelect(unit)
    end
    self:setHudDirty()
    return true
end

-- ── enemy AI turn ─────────────────────────────────────────────────────────
--
-- The AI reuses the same reachable() / damage code the player does, and plays
-- its plan through the same move path, so there is one movement and one
-- damage implementation. It just skips the cursor and menus.

function Script:beginAiTurn(unit)
    self.phase = "ai"
    -- The AI plans against the same reachable set the player would see; keep
    -- the parent map so its chosen cell can be walked to through pathTo.
    self.reachCells, self.reachDist, self.reachParent = self:reachable(unit)
    self.aiPlan = BattleAI.plan(self, unit)
    self.aiTimer = 0.25   -- a short pause so the turn reads as deliberate
    self:clearHighlights()
    self:setHudDirty()
end

function Script:updateAi(dt)
    self.aiTimer = self.aiTimer - dt
    if self.aiTimer > 0 then
        return
    end
    local plan = self.aiPlan
    if plan and plan.move then
        -- Walk to the planned cell through the normal move path.
        self.path = self:pathTo(plan.move.x, plan.move.y, self.reachParent)
        self.pathIndex = 1
        self.moveProgress = 0
        self.moveDest = { x = plan.move.x, y = plan.move.y }
        self.phase = "aiMove"
        self:clearHighlights()
        return
    end
    self:resolveAiHit()
end

function Script:resolveAiHit()
    local plan = self.aiPlan
    if plan and plan.attack and plan.attack:isAlive() then
        self:resolveHit(plan.attack, self.active.atk, false)
    end
    self.aiPlan = nil
    self:finishTurn()
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
    -- Offer Attack only when something is in reach: an Attack entry that can
    -- never resolve would bounce the player back to the menu forever. Same for
    -- the skill. Wait is always available.
    self.menu = {}
    local inReach = self:targetCells(self.active.range)
    if #inReach > 0 then
        self.menu[#self.menu + 1] = "attack"
    end
    if self.active:hasSkill() and self.active:canUseSkill() then
        local inSkillReach = self:targetCells(self.active.skillRange)
        if #inSkillReach > 0 then
            self.menu[#self.menu + 1] = "skill"
        end
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
        -- Repaint the roster too: the killing blow lands in resolveHit and
        -- finishTurn returns here without the usual setHudDirty, so without
        -- this the last victim's row still reads as alive under the banner.
        self:setHudDirty()
        local hud = ui.get(self.hudName)
        if type(hud) == "table" and hud.showOutcome then
            hud:showOutcome(self.outcome)
        end
        -- Publish the result for the overworld to consume on return (M1).
        Persist.battle = Persist.battle or {}
        Persist.battle.lastOutcome = self.outcome
        Persist.battle.lastScene = BATTLE_SCENE
        -- Grow the party (G1): a victory grants exp, and a party unit carries
        -- its ending hp back so the next battle starts where this one left off.
        if self.outcome == "victory" then
            self:grantVictoryReward()
        end
        self:carryPartyState()
        -- Hand the outcome to the quest system (G4). A victory advances every
        -- active "defeat monsters" objective by the number of enemies this
        -- board actually held (all of them are down on a victory), so the
        -- objective measures monsters defeated, not battles won -- one cleared
        -- board is one completed objective, not one of three. A visit to this
        -- scene is recorded on the way in (onInit), not here.
        if self.outcome == "victory" then
            local defeated = 0
            for _, unit in ipairs(self.units) do
                if unit.team == "enemy" then
                    defeated = defeated + 1
                end
            end
            Quests.notify("battle_victory", defeated, { scene = BATTLE_SCENE })
        end
        -- And, when this battle was entered from a scene, go back after a beat.
        if self.returnScene ~= nil and self.returnScene ~= "" then
            self.returnTimer = self.returnDelay or 2.5
        end
        return true
    end
    return false
end

-- Exp from every defeated enemy (a simple per-unit value on the unit script).
function Script:grantVictoryReward()
    local total = 0
    for _, unit in ipairs(self.units) do
        if unit.team == "enemy" then
            total = total + (unit.expReward or 10)
        end
    end
    self.levelsGained = {}
    for _, unit in ipairs(self.units) do
        if unit.team == "player" and unit.partyId ~= nil and unit.partyId ~= "" and unit:isAlive() then
            local gained = require("Party").gainExp(unit.partyId, total)
            for _, level in ipairs(gained) do
                self.levelsGained[#self.levelsGained + 1] = level
                log:info("Battle: " .. tostring(unit.displayName) .. " reached level " .. tostring(level.level))
            end
        end
    end
    -- Best-effort feedback: a banner if the HUD has one.
    local hud = ui.get(self.hudName)
    if #self.levelsGained > 0 and type(hud) == "table" and hud.showHint then
        local first = self.levelsGained[1]
        hud:showHint("Level up! Lv " .. tostring(first.level))
    end
    -- And a drop (G2): one item from the table into the bag, so the equipment
    -- screen has something to wear. A victory with no table rolls nothing.
    self.lootId = require("Loot").award(self.lootTable, self.lootIndex)
    if self.lootId ~= nil and type(hud) == "table" and hud.showHint and #self.levelsGained == 0 then
        hud:showHint("Obtained: " .. tostring(self.lootId))
    end
end

-- Write each party unit's ending hp back into the roster (G1 carry-over).
function Script:carryPartyState()
    local Party = require("Party")
    for _, unit in ipairs(self.units) do
        if unit.team == "player" and unit.partyId ~= nil and unit.partyId ~= "" then
            Party.setHp(unit.partyId, unit.hp)
        end
    end
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

-- A permanent grid veil: one faint-bordered quad per cell, pooled and reused
-- by the highlight painter. The board always reads as cells (the genre's
-- baseline); selection tints ride the same pool.
function Script:ensureGrid()
    if self.bGridReady then
        return
    end
    self.bGridReady = true
    self.grid = {}
    for y = 0, self.height - 1 do
        for x = 0, self.width - 1 do
            local entity = world.spawnSprite("GridCell",
                { texture = self.gridTexture, size = Vec2.new(1.0, 1.0) })
            if not entity then
                return
            end
            local sprite = entity:getSprite2D()
            if sprite then
                sprite.bYSort = true
                sprite.sortOrder = -10
                sprite.tint = Vec4.new(1.0, 1.0, 1.0, 1.0)
            end
            local p = self.map:cellToWorld(x, y)
            entity:getTransform():setPosition(Vec3.new(p.x, p.y, 0.005))
            self.grid[#self.grid + 1] = entity
        end
    end
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
    -- Entering this board satisfies a "visit the ruins" objective (G4). The
    -- notify is a no-op until the quest is active, so a standalone battle test
    -- (no quest accepted) is unaffected.
    Quests.notify("visited", 1, { scene = BATTLE_SCENE })
end

function Script:updateSelect(dt)
    -- Mouse first: hover moves the cursor, left click confirms, right click
    -- is a no-op here (nothing to cancel before moving). The pointer only
    -- steers after it actually moved (see pointerMoved).
    if self:pointerMoved() then
        local hover = self:mouseCell()
        if hover and (hover.x ~= self.cursor.x or hover.y ~= self.cursor.y) then
            self.cursor.x, self.cursor.y = hover.x, hover.y
        end
    end
    self:cursorStep()
    local confirm = self:confirmEdge()
    if not confirm and self:mouseLeftPressed() then
        confirm = true
    end
    if not confirm then
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
        self.moveDest = { x = self.cursor.x, y = self.cursor.y }
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
        -- Path exhausted: land on the destination the path targeted.
        local dest = self.moveDest or unit.cell
        unit.cell = { x = dest.x, y = dest.y }
        if self.phase == "aiMove" then
            self:resolveAiHit()
        else
            self.phase = "done"
            self:enterMenu()
        end
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

-- Number keys pick an action directly (the bottom bar prints them); arrows
-- walk the highlight and Space confirms, so the pointer is never required.
local MENU_HOTKEYS = {
    [EKey.K_1] = 1,
    [EKey.K_2] = 2,
    [EKey.K_3] = 3,
}

function Script:updateMenu(dt)
    if input:isActionPressed("move_left") then
        self.menuIndex = self.menuIndex - 1
        if self.menuIndex < 1 then
            self.menuIndex = #self.menu
        end
        self:setHudDirty()
    elseif input:isActionPressed("move_right") then
        self.menuIndex = self.menuIndex + 1
        if self.menuIndex > #self.menu then
            self.menuIndex = 1
        end
        self:setHudDirty()
    end
    for key, index in pairs(MENU_HOTKEYS) do
        if input:isKeyPressed(key) and self.menu[index] then
            self.menuIndex = index
            self:setHudDirty()
            self:chooseMenu(index)
            return
        end
    end
    if self:mouseRightPressed() then
        -- Cancel back to movement: the unit stays where it walked to, but the
        -- player may reconsider (the button must be released before the next
        -- right-click counts, via the next frame's edge check).
        self.bCancelArmed = false
        self:enterSelect(self.active)
        return
    end
    if not self:confirmEdge() then
        return
    end
    self:chooseMenu(self.menuIndex)
end

function Script:chooseMenu(index)
    local choice = self.menu[index]
    if choice == "attack" then
        self:enterTarget(self.active.atk, self.active.range, false)
    elseif choice == "skill" then
        self:enterTarget(self.active.skillPower, self.active.skillRange, true)
    else
        self:finishTurn()
    end
end

function Script:updateTarget(dt)
    if self:pointerMoved() then
        local hover = self:mouseCell()
        if hover then
            -- Hover steps the cursor across the reachable target cells only.
            local k = cellKey(hover.x, hover.y)
            if self.targetIndex[k] and (hover.x ~= self.cursor.x or hover.y ~= self.cursor.y) then
                self.cursor.x, self.cursor.y = hover.x, hover.y
            end
        end
    end
    self:cursorStep()
    local confirm = self:confirmEdge()
    if not confirm and self:mouseLeftPressed() then
        confirm = true
    end
    if self:mouseRightPressed() then
        self:enterMenu()
        return
    end
    if not confirm then
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
    -- Once the outcome is up and a return scene is authored, hold the banner
    -- for a beat, then hand the party back (M1).
    if self.returnTimer then
        self.returnTimer = self.returnTimer - dt
        if self.returnTimer <= 0 then
            self.returnTimer = nil
            world.loadScene(self.returnScene, self.returnSpawn or "", self.returnActor or "")
            return
        end
    end
    if not self.bCollected then
        self:collectUnits()
        self:ensureGrid()
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
    elseif self.phase == "move" or self.phase == "aiMove" then
        self:updateMove(dt)
    elseif self.phase == "menu" then
        self:updateMenu(dt)
    elseif self.phase == "target" then
        self:updateTarget(dt)
    elseif self.phase == "ai" then
        self:updateAi(dt)
    end
end

return Script
