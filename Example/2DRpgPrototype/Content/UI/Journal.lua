-- Lists Persist.quests. J toggles the panel. Dialogue owns the screen
-- while it is up, so the key does nothing then.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Locale = require("Locale")
local Script = ScriptBase:new()

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

function Script:refresh()
    local lines = Quests.lines()
    if #lines == 0 then
        self.body.text = Locale.tr("No quests.")
    else
        self.body.text = table.concat(lines, "\n")
    end
end

function Script:applyLocale()
    if self.title then
        self.title.text = Locale.tr("Journal")
    end
    if self.bOpen then
        self:refresh()
    end
end

function Script:onInit()
    self.body = self:find("Body")
    self.title = self:find("Title")
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

return Script
