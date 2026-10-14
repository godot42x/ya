-- Journal.lua -- the quest log (G4). J toggles the panel; while it is up the
-- player frees the keys (Player.lua freezes movement on busy()).
--
-- Left column  QuestRow1..8  -- one row per accepted quest, active first then
--                               completed, each in the order it was accepted.
-- Right column Body          -- the selected quest: its title, description,
--                               one line per objective (with count/target) and
--                               the reward line.
--
-- The panel is read-only by design: it shows state, it never changes it (no
-- "abandon quest"). It owns no game state either -- every row is rebuilt from
-- Quests, so a quest advanced by a battle and one advanced by a talk paint the
-- same way.
--
-- The Body text keeps the v1 shape -- `title` then the description, prefixed
-- "Done." once complete -- and appends the objective/reward lines under it, so
-- a quest with no objectives reads exactly as it did before this panel grew a
-- list.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Locale = require("Locale")
local Script = ScriptBase:new()

local ROW_COUNT = 8
local COLOR_ACTIVE = Vec4.new(0.96, 0.96, 0.94, 1.0)
local COLOR_DONE = Vec4.new(0.58, 0.6, 0.64, 1.0)
local COLOR_SELECTED = Vec4.new(0.98, 0.86, 0.45, 1.0)

local function dialogueBusy()
    local dialogue = ui.get("Dialogue")
    return type(dialogue) == "table" and dialogue.busy ~= nil and dialogue:busy()
end

local function settingsBusy()
    local settings = ui.get("Settings")
    return type(settings) == "table" and settings.busy ~= nil and settings:busy()
end

function Script:busy()
    return self.bOpen == true
end

-- Active quests first, then completed, each in accept order.
function Script:collect()
    local rows = {}
    for _, entry in ipairs(Quests.getActive()) do
        rows[#rows + 1] = entry
    end
    for _, entry in ipairs(Quests.getCompleted()) do
        rows[#rows + 1] = entry
    end
    return rows
end

function Script:selected()
    local rows = self.rows or {}
    local entry = rows[self.index]
    if entry == nil then
        self.index = 1
        entry = rows[1]
    end
    return entry
end

-- The right-hand text for one quest: title, then the description (prefixed
-- once complete), then the objectives and the reward.
function Script:bodyFor(entry)
    if entry == nil then
        return Locale.tr("No quests.")
    end
    local lines = { Locale.tr(entry.title) }
    local desc = Locale.tr(entry.state == "active" and entry.active or entry.done)
    if entry.state ~= "active" then
        lines[#lines + 1] = Locale.tr("Done. ") .. desc
    else
        lines[#lines + 1] = desc
    end
    for _, objective in ipairs(entry.objectives or {}) do
        lines[#lines + 1] = Quests.objectiveLine(objective)
    end
    local reward = Quests.rewardLine(entry)
    if reward ~= "" then
        lines[#lines + 1] = reward
    end
    return table.concat(lines, "\n")
end

function Script:paintRows()
    for i, row in ipairs(self.rowButtons or {}) do
        local entry = (self.rows or {})[i]
        if entry then
            local selected = i == self.index
            row.label.text = (selected and "> " or "  ") .. Locale.tr(entry.title)
            if selected then
                row.label.color = COLOR_SELECTED
            elseif entry.state == "active" then
                row.label.color = COLOR_ACTIVE
            else
                row.label.color = COLOR_DONE
            end
            row.button.visible = true
        else
            row.label.text = ""
            row.label.color = COLOR_DONE
            row.button.visible = false
        end
    end
end

function Script:refresh()
    self.rows = self:collect()
    if self.index < 1 then
        self.index = 1
    elseif self.index > #self.rows then
        self.index = #self.rows
    end
    self:paintRows()
    self.body.text = self:bodyFor(self:selected())
end

function Script:applyLocale()
    if self.title then
        self.title.text = Locale.tr("Journal")
    end
    if self.bOpen then
        self:refresh()
    end
end

function Script:moveSelection(delta)
    if #(self.rows or {}) == 0 then
        return
    end
    self.index = self.index + delta
    if self.index < 1 then
        self.index = #self.rows
    elseif self.index > #self.rows then
        self.index = 1
    end
    self:refresh()
end

function Script:onInit()
    self.body = self:find("Body")
    self.title = self:find("Title")
    self.rowButtons = {}
    for i = 1, ROW_COUNT do
        local button = self:find("QuestRow" .. i)
        local label = self:find("QuestRow" .. i .. "Label")
        if button and label then
            local index = i
            self.rowButtons[#self.rowButtons + 1] = { button = button, label = label }
            button.onClicked:add(self, function()
                self.index = index
                self:refresh()
            end)
        end
    end
    self.index = 1
    self.rows = {}
    self.bOpen = false
    self.root.visible = false
    self:applyLocale()
    -- A hidden widget is not ticked. The timer still runs, so the toggle
    -- key is seen while the panel is closed.
    self:every(0.001, function()
        if not input:isActionPressed("journal") or dialogueBusy() or settingsBusy() then
            return
        end
        self.bOpen = not self.bOpen
        self.root.visible = self.bOpen
        if self.bOpen then
            self:refresh()
        end
    end)
end

function Script:onUpdate(dt)
    if not self.bOpen then
        return
    end
    if input:isActionPressed("move_up") then
        self:moveSelection(-1)
    elseif input:isActionPressed("move_down") then
        self:moveSelection(1)
    end
end

return Script
