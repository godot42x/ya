-- Battle.lua -- the turn controller for one battle scene. It owns no unit
-- list: the board is the roster. It walks the tilemap's cells, asks
-- `map:entityAt(x, y)` who stands there, and reads that unit's own script
-- (`team` / `spd` / `hp` / `displayName`). Turn order is speed, highest first.
--
-- T1 scope: collect the roster, order it, and step through it on confirm.
-- Moving and attacking are added in T2/T3; this file grows with them.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.mapName = "TilemapGround"
Script.hudName = "BattleHUD"
Script:properties({
    mapName = { type = "string", tooltip = "Tilemap entity holding the board" },
    hudName = { type = "string", tooltip = "Game UI entry showing the turn order" },
})

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
    self.bHudDirty = true
    return true
end

function Script:endTurn()
    if self.active then
        self.active:setActive(false)
        self.active = nil
    end
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
        self.bReady = false
        return
    end
    self.map = self.map:getTilemap()
    self.width = self.map.width
    self.height = self.map.height
    self.round = 1
    self.index = 1
    self.units = {}
    self.active = nil
    self.bReady = false
    self.bArmed = true
    self.bHudDirty = false
    -- Units run their own onInit; defer collection to the first update so
    -- every unit's hp exists before we read it.
    self.bCollected = false
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
        self.bArmed = false
        return
    end
    -- Repaint until the HUD entry's script answers, then only on change.
    if self.bHudDirty or self.bHudPending then
        self.bHudPending = not self:refreshHud()
        if not self.bHudPending then
            self.bHudDirty = false
        end
    end
    if not input:isActionDown("confirm") then
        self.bArmed = true
    end
    if self.bArmed and input:isActionPressed("confirm") then
        self.bArmed = false
        self:endTurn()
    end
end

return Script
