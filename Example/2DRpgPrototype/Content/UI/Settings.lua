-- Esc opens this card (the player script's onCancel). Space switches
-- Persist.locale between English and Chinese. The card sits above the
-- other panels, so a second Esc hits this script's onCancel and closes it.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

function Script:busy()
    return self.bOpen == true
end

function Script:refresh()
    if self.title then
        self.title.text = Locale.tr("Settings")
    end
    if self.body then
        local name = Locale.current() == "zh" and "中文" or "English"
        self.body.text = Locale.tr("Language") .. "\n" .. name
    end
    if self.hint then
        self.hint.text = "Esc"
    end
end

function Script:applyLocale()
    self:refresh()
end

function Script:open()
    self.bOpen = true
    self.root.visible = true
    self:refresh()
end

function Script:close()
    self.bOpen = false
    self.root.visible = false
end

function Script:toggle()
    if self.bOpen then
        self:close()
    else
        self:open()
    end
end

function Script:onCancel()
    if not self.bOpen then
        return false
    end
    self:close()
    return true
end

function Script:onInit()
    self.title = self:find("Title")
    self.body = self:find("Body")
    self.hint = self:find("Hint")
    self.bOpen = false
    self.root.visible = false
    self:setTickEnabled(true)
end

function Script:onUpdate(dt)
    if not self.bOpen then
        return
    end
    if input:isActionPressed("confirm") then
        Locale.toggle()
    end
end

return Script
