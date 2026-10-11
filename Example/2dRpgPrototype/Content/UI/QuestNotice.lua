-- A short banner. Quests.start / Quests.complete call show() when the
-- state actually changes. It does not freeze the player.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

local HOLD_SECONDS = 2.5

function Script:show(heading, title)
    self.heading = heading or ""
    self.titleSource = title or ""
    self:applyLocale()
    self.root.visible = true
    self.token = (self.token or 0) + 1
    local token = self.token
    self:after(HOLD_SECONDS, function()
        if self.token ~= token then
            return
        end
        self.root.visible = false
    end)
end

function Script:applyLocale()
    if self.heading == nil then
        return
    end
    self.title.text = Locale.tr(self.heading)
    self.body.text = Locale.tr(self.titleSource or "")
end

function Script:onInit()
    self.title = self:find("Title")
    self.body = self:find("Body")
    self.token = 0
    self.root.visible = false
end

return Script
