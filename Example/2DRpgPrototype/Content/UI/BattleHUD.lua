-- BattleHUD.lua -- the battle HUD. Battle.lua pushes state here; this paints
-- it. Layout follows the reference genre HUDs (SRPG / CRPG): a framed party
-- card top-left, a large round badge top-right, the action bar bottom-centre,
-- and the turn order down the left edge. It owns no game state.
--
--   hud:setTurnOrder(round, units, activeIndex)
--   hud:showActive(name, {hp, maxHp, team})
--   hud:showMenu(items, selected)   -- items: {id, label, hotkey, enabled}
--   hud:showHint(text)
--   hud:showOutcome("victory" | "defeat")
--   hud:setMenuReceiver(receiver)   -- receiver:onMenuChosen(id)
--
-- The panels are Kenney UI Pack nine-patches (Content/Textures/UI), tinted
-- per element, so one texture carries every panel at any size.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

local COLOR_ACTIVE = Vec4.new(0.98, 0.86, 0.45, 1.0)
local COLOR_PLAYER = Vec4.new(0.80, 0.92, 1.0, 1.0)
local COLOR_ENEMY = Vec4.new(1.0, 0.62, 0.56, 1.0)
local COLOR_DOWN = Vec4.new(0.55, 0.55, 0.58, 1.0)
local COLOR_VICTORY = Vec4.new(0.62, 0.95, 0.66, 1.0)
local COLOR_DEFEAT = Vec4.new(0.95, 0.5, 0.45, 1.0)

-- Kenney panels: 192x64 with 16px caps fits any width/height when sliced.
local PANEL_TEXTURE = "Content:Textures/UI/button_rectangle_depth_flat.png"
local PANEL_MARGIN = Vec4.new(16.0, 16.0, 16.0, 16.0)

local function hpBarWidth(ratio)
    return math.max(0.0, math.min(1.0, ratio))
end

-- ── paint ─────────────────────────────────────────────────────────────────

function Script:paintPartyCard()
    local unit = self.activeUnit
    if not self.activeNameLabel then
        return
    end
    if not unit then
        self.activeNameLabel.text = Locale.tr("Acting") .. ": -"
        if self.activeHpLabel then
            self.activeHpLabel.text = ""
            self.activeHpLabel.visible = false
        end
        if self.activeHpFill then
            self.activeHpFill.visible = false
        end
        return
    end
    self.activeNameLabel.text = tostring(unit.displayName or self.activeName or "?")
    local hp, maxHp = unit.hp or 0, unit.maxHp or 1
    if self.activeHpLabel then
        self.activeHpLabel.text = string.format("%d/%d", hp, maxHp)
        self.activeHpLabel.visible = true
    end
    if self.activeHpFill then
        -- The bar track is 132 wide; the fill is a left-pivoted child whose
        -- width tracks the ratio. Both are world-sized for this reference
        -- resolution (1280x720).
        local ratio = maxHp > 0 and hp / maxHp or 0
        self.activeHpFill.visible = true
        self.activeHpFill.fill = ratio < 0.34 and Vec4.new(0.92, 0.36, 0.30, 1.0)
            or (unit.team == "enemy" and Vec4.new(0.95, 0.55, 0.30, 1.0)
            or Vec4.new(0.42, 0.85, 0.46, 1.0))
        self.activeHpFill:setSize(math.max(2.0, 132.0 * hpBarWidth(ratio)), 12.0)
    end
end

function Script:paintOrder()
    for i, row in ipairs(self.orderRows or {}) do
        local unit = (self.order or {})[i]
        if unit then
            local down = (unit.hp or 0) <= 0
            local mark = (i == self.activeIndex) and "▶ " or ""
            -- Down units keep their row (the order is the roster) and say so:
            -- the dim colour alone does not survive every panel size.
            local name = tostring(unit.displayName or "?")
            if down then
                name = name .. "  (down)"
            end
            row.label.text = string.format("%s%s", mark, name)
            if down then
                row.label.color = COLOR_DOWN
            elseif i == self.activeIndex then
                row.label.color = COLOR_ACTIVE
            elseif unit.team == "enemy" then
                row.label.color = COLOR_ENEMY
            else
                row.label.color = COLOR_PLAYER
            end
            row.label.visible = true
        else
            row.label.visible = false
        end
    end
end

-- The bottom action bar: one label per entry, hotkey in brackets. The
-- highlighted entry gets the accent color; disabled ones fade.
function Script:paintMenu()
    for i, row in ipairs(self.menuRows or {}) do
        local item = (self.menu or {})[i]
        -- An entry the phase does not offer: hide the whole button, not just
        -- its label, or an empty chip sits in the bar.
        if row.button then
            row.button.visible = item ~= nil
        end
        if item then
            local label = Locale.tr(item.label)
            if item.hotkey and item.hotkey ~= "" then
                label = string.format("[%s] %s", item.hotkey, label)
            end
            row.label.text = (i == self.menuIndex and "> " or "  ") .. label
            if not item.enabled then
                row.label.color = COLOR_DOWN
            elseif i == self.menuIndex then
                row.label.color = COLOR_ACTIVE
            else
                row.label.color = Vec4.new(0.92, 0.92, 0.94, 1.0)
            end
            row.label.visible = true
        else
            row.label.text = ""
            row.label.visible = false
        end
    end
end

function Script:setTurnOrder(round, units, activeIndex)
    self.round = round
    self.order = units
    self.activeIndex = activeIndex
    if self.roundLabel then
        self.roundLabel.text = tostring(round)
    end
    self:paintOrder()
end

function Script:showActive(name, unit)
    self.activeName = name
    self.activeUnit = unit
    self:paintPartyCard()
end

function Script:showMenu(items, selected)
    self.menu = items or {}
    self.menuIndex = selected or 1
    self:paintMenu()
end

function Script:showHint(text)
    if self.hintText then
        self.hintText.text = Locale.tr(text or "")
    end
end

function Script:setMenuReceiver(receiver)
    self.menuReceiver = receiver
end

-- A click on an action row. Battle.lua owns what the id means.
function Script:onMenuRow(index)
    local item = (self.menu or {})[index]
    if not item or not item.enabled then
        return
    end
    if self.menuReceiver and self.menuReceiver.onMenuChosen then
        self.menuReceiver:onMenuChosen(item.id)
    end
end

function Script:showOutcome(outcome)
    if self.outcomeBox then
        self.outcomeBox.visible = true
    end
    if self.outcome then
        self.outcome.text = Locale.tr(outcome == "victory" and "Victory!" or "Defeat...")
        self.outcome.color = (outcome == "victory") and COLOR_VICTORY or COLOR_DEFEAT
        self.outcome.visible = true
    end
end

function Script:applyLocale()
    self:paintPartyCard()
    self:paintOrder()
    self:paintMenu()
    self:showHint(self.hint or "")
end

-- ── setup ─────────────────────────────────────────────────────────────────

function Script:stylePanel(widget, tint)
    if widget and widget.setFillImage then
        widget:setFillImage(PANEL_TEXTURE, PANEL_MARGIN, tint)
    end
end

function Script:onInit()
    self.roundLabel   = self:find("RoundBadge")
    self.roundCaption = self:find("RoundCaption")
    self.activeNameLabel = self:find("ActiveName")
    self.activeHpLabel   = self:find("ActiveHp")
    self.activeHpFill    = self:find("ActiveHpFill")
    self.hintText     = self:find("Hint")
    self.outcome      = self:find("Outcome")
    self.outcomeBox   = self:find("OutcomeBox")
    if self.outcomeBox then
        self.outcomeBox.visible = false
    end
    if self.outcome then
        self.outcome.visible = false
    end

    -- Kenney nine-patch skins, darkened into the battle palette: the texture
    -- is mid-grey, so the tint is the panel's own color (alpha keeps it a
    -- chrome frame, not a slab over the board).
    self:stylePanel(self:find("PartyCard"), Vec4.new(0.34, 0.37, 0.46, 0.96))
    self:stylePanel(self:find("RoundPanel"), Vec4.new(0.42, 0.38, 0.26, 0.96))
    self:stylePanel(self:find("OrderPanel"), Vec4.new(0.30, 0.33, 0.42, 0.92))
    self:stylePanel(self:find("ActionBar"), Vec4.new(0.32, 0.34, 0.42, 0.96))
    for i = 1, 4 do
        local button = self:find("ActionButton" .. i)
        if button and button.setFillImage then
            button:setFillImage(PANEL_TEXTURE, PANEL_MARGIN, Vec4.new(0.55, 0.58, 0.70, 1.0))
        end
    end
    -- Outcome + the HP track stay solid paints: they are gauges, not chrome.
    if self.activeHpFill then
        self.activeHpFill.fill = Vec4.new(0.42, 0.85, 0.46, 1.0)
    end

    self.orderRows = {}
    for i = 1, 6 do
        local label = self:find("Order" .. i)
        if label then
            self.orderRows[#self.orderRows + 1] = { label = label }
        end
    end
    self.menuRows = {}
    for i = 1, 4 do
        local button = self:find("ActionButton" .. i)
        local label  = self:find("Action" .. i)
        if button and label then
            self.menuRows[#self.menuRows + 1] = { label = label, button = button }
            -- Click = pick that action (same id the receiver already handles).
            button.onClicked:add(self, function()
                self:onMenuRow(i)
            end)
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
