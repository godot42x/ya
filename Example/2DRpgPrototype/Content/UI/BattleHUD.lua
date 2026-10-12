-- BattleHUD.lua -- widget script of the battle turn panel. Battle.lua pushes
-- the ordered roster here; this only paints it. It owns no game state.
--
--   ui.get("BattleHUD"):setTurnOrder(round, units, activeIndex)
--   ui.get("BattleHUD"):showActive(name)
--
-- A unit is `{ displayName = string, hp = number, maxHp = number, team = string }`.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

local COLOR_ACTIVE = Vec4.new(0.95, 0.84, 0.45, 1.0)
local COLOR_PLAYER = Vec4.new(0.75, 0.9, 1.0, 1.0)
local COLOR_ENEMY = Vec4.new(1.0, 0.6, 0.55, 1.0)

local function hpBar(unit)
    local hp = unit.hp or 0
    local maxHp = unit.maxHp or 1
    if maxHp <= 0 then
        maxHp = 1
    end
    local filled = math.floor(10 * hp / maxHp + 0.5)
    local bar = ""
    for i = 1, 10 do
        bar = bar .. (i <= filled and "#" or "-")
    end
    return string.format("%s %d/%d [%s]", tostring(unit.displayName or "?"), hp, maxHp, bar)
end

function Script:paint()
    if not self.order then
        return
    end
    if self.title then
        self.title.text = string.format("%s %d", Locale.tr("Round"), self.round or 1)
    end
    -- One line per unit, active first-marked. A fresh table each paint.
    local rows = {}
    for i, unit in ipairs(self.order) do
        local mark = (i == self.activeIndex) and "> " or "  "
        local name = tostring(unit.displayName or "?")
        if (unit.hp or 0) <= 0 then
            rows[#rows + 1] = { text = mark .. name .. "  (down)", active = (i == self.activeIndex), dead = true }
        else
            rows[#rows + 1] = { text = mark .. hpBar(unit), active = (i == self.activeIndex), team = unit.team }
        end
    end
    if self.order == nil or #rows == 0 then
        return
    end
    -- The first N Body_ lines exist in the document; extra units are dropped.
    for i, row in ipairs(self.rows) do
        local data = rows[i]
        if data then
            row.line.text = data.text
            if data.active then
                row.line.color = COLOR_ACTIVE
            elseif data.dead then
                row.line.color = Vec4.new(0.5, 0.5, 0.5, 1.0)
            elseif data.team == "enemy" then
                row.line.color = COLOR_ENEMY
            else
                row.line.color = COLOR_PLAYER
            end
            row.line.visible = true
        else
            row.line.text = ""
            row.line.visible = false
        end
    end
end

function Script:setTurnOrder(round, units, activeIndex)
    self.round = round
    self.order = units
    self.activeIndex = activeIndex
    self:paint()
end

function Script:showActive(name)
    if self.header then
        self.header.text = Locale.tr("Acting") .. ": " .. tostring(name)
    end
end

function Script:applyLocale()
    self:paint()
end

function Script:onInit()
    self.title = self:find("Title")
    self.header = self:find("Header")
    self.rows = {}
    for i = 1, 6 do
        local line = self:find("Row" .. i)
        if line then
            self.rows[#self.rows + 1] = { line = line }
        end
    end
    self.order = nil
    self.round = 1
    self.activeIndex = 1
    self.root.visible = true
end

return Script
