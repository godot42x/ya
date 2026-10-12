-- BattleHUD.lua -- widget script of the battle turn panel. Battle.lua pushes
-- the ordered roster, the action menu and the result here; this only paints
-- them. It owns no game state.
--
--   ui.get("BattleHUD"):setTurnOrder(round, units, activeIndex)
--   ui.get("BattleHUD"):showActive(name)
--   ui.get("BattleHUD"):showMenu(names, selectedIndex)   -- nil/{} clears it
--   ui.get("BattleHUD"):showHint(text)
--   ui.get("BattleHUD"):showOutcome("victory" | "defeat")
--
-- A unit is `{ displayName, hp, maxHp, team }`.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

local COLOR_ACTIVE = Vec4.new(0.95, 0.84, 0.45, 1.0)
local COLOR_PLAYER = Vec4.new(0.75, 0.9, 1.0, 1.0)
local COLOR_ENEMY = Vec4.new(1.0, 0.6, 0.55, 1.0)
local COLOR_MENU = Vec4.new(0.9, 0.9, 0.92, 1.0)
local COLOR_MENU_SEL = Vec4.new(0.95, 0.84, 0.45, 1.0)
local COLOR_VICTORY = Vec4.new(0.6, 0.95, 0.65, 1.0)
local COLOR_DEFEAT = Vec4.new(0.95, 0.5, 0.45, 1.0)

local function hpBar(unit)
    local hp, maxHp = unit.hp or 0, unit.maxHp or 1
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
    if self.title then
        self.title.text = string.format("%s %d", Locale.tr("Round"), self.round or 1)
    end
    local rows = {}
    for i, unit in ipairs(self.order or {}) do
        local mark = (i == self.activeIndex) and "> " or "  "
        local name = tostring(unit.displayName or "?")
        if (unit.hp or 0) <= 0 then
            rows[#rows + 1] = { text = mark .. name .. "  (down)", dead = true }
        else
            rows[#rows + 1] = { text = mark .. hpBar(unit), team = unit.team,
                               active = (i == self.activeIndex) }
        end
    end
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
    self:paintMenu()
end

function Script:paintMenu()
    if not self.menuRows then
        return
    end
    local names = self.menu or {}
    for i, row in ipairs(self.menuRows) do
        if names[i] then
            local label = Locale.tr(names[i]:sub(1, 1):upper() .. names[i]:sub(2))
            row.line.text = (i == self.menuIndex and "> " or "  ") .. label
            row.line.color = (i == self.menuIndex) and COLOR_MENU_SEL or COLOR_MENU
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

function Script:showMenu(names, selected)
    self.menu = names or {}
    self.menuIndex = selected or 1
    self:paintMenu()
end

function Script:showHint(text)
    if self.hintText then
        self.hintText.text = Locale.tr(text or "")
    end
end

function Script:showOutcome(outcome)
    if self.outcome then
        self.outcome.text = Locale.tr(outcome == "victory" and "Victory!" or "Defeat...")
        self.outcome.color = (outcome == "victory") and COLOR_VICTORY or COLOR_DEFEAT
        self.outcome.visible = true
    end
end

function Script:applyLocale()
    self:paint()
end

function Script:onInit()
    self.title = self:find("Title")
    self.header = self:find("Header")
    self.hintText = self:find("Hint")
    self.outcome = self:find("Outcome")
    if self.outcome then
        self.outcome.visible = false
    end
    self.rows = {}
    for i = 1, 6 do
        local line = self:find("Row" .. i)
        if line then
            self.rows[#self.rows + 1] = { line = line }
        end
    end
    self.menuRows = {}
    for i = 1, 3 do
        local line = self:find("Menu" .. i)
        if line then
            self.menuRows[#self.menuRows + 1] = { line = line }
        end
    end
    self.order = {}
    self.menu = {}
    self.round = 1
    self.activeIndex = 1
    self.menuIndex = 1
    self.root.visible = true
end

return Script
