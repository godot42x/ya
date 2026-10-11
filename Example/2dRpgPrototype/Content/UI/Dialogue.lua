-- Dialogue.lua
-- Widget script of the Dialogue entry. Gameplay puts words on screen:
--
--   ui.get("Dialogue"):say(script, onDone)
--
-- `script` is an array of pages, or a table with `start` naming the first
-- page. A page is a string, or `{ text, next, choices, onShow }`. `choices` is
-- `{ { text, next }, ... }`. `onShow`, when it is a function, runs once as
-- that page opens. `next` is the following page's key; omit it to
-- advance an array, or to close a named page. Choices are their own rows on
-- the right of the box, and they appear when the page opens. Confirm finishes
-- the typewriter, then either picks the highlighted choice or advances.
-- Up / Down move the highlight. The frame that opened the box ignores confirm.
local ScriptBase = require("ScriptBase")
local Locale = require("Locale")
local Script = ScriptBase:new()

local REVEAL_SECONDS = 0.02
local HINT_ADVANCE = "Space / Enter / E"
local HINT_CHOOSE = "Up / Down, Space"
local COLOR_ON = Vec4.new(0.95, 0.84, 0.45, 1)
local COLOR_OFF = Vec4.new(0.96, 0.96, 0.94, 1)

-- The script state does not publish the utf8 library. Walk lead bytes.
local function codePointEnd(text, count)
    local i = 1
    local n = #text
    local seen = 0
    while i <= n and seen < count do
        local b = string.byte(text, i)
        if b < 0x80 then
            i = i + 1
        elseif b < 0xE0 then
            i = i + 2
        elseif b < 0xF0 then
            i = i + 3
        else
            i = i + 4
        end
        seen = seen + 1
    end
    return i - 1
end

local function codePointCount(text)
    local count = 0
    local i = 1
    local n = #text
    while i <= n do
        local b = string.byte(text, i)
        if b < 0x80 then
            i = i + 1
        elseif b < 0xE0 then
            i = i + 2
        elseif b < 0xF0 then
            i = i + 3
        else
            i = i + 4
        end
        count = count + 1
    end
    return count
end

local function confirmPressed()
    -- isActionPressed is the down edge (isActionDown is the held state).
    return input:isActionPressed("confirm")
end

local function settingsBusy()
    local settings = ui.get("Settings")
    return type(settings) == "table" and settings.busy ~= nil and settings:busy()
end

-- True while a dialogue is on screen; the player script holds movement by
-- polling this (game-ui S4 modal entries will take over that hold).
function Script:busy()
    return self.script ~= nil
end

function Script:say(script, onDone)
    if type(script) ~= "table" then
        log:warn("Dialogue:say expects a page table")
        return
    end
    local first
    if type(script.start) == "string" then
        first = script.start
    elseif #script > 0 then
        first = 1
    else
        log:warn("Dialogue:say expects an array or a start page")
        return
    end
    self.script = script
    self.onDone = onDone
    self.openedFrame = time:getFrameIndex()
    self.root.visible = true
    self:showPage(first)
end

function Script:showPage(id)
    local raw = self.script[id]
    local page = raw
    if type(raw) == "string" then
        page = { text = raw }
    elseif type(raw) ~= "table" then
        log:warn("Dialogue: no page '" .. tostring(id) .. "'")
        self:close()
        return
    end
    self.pageId = id
    self.page = page
    self.bChoicesShown = false
    self.choiceIndex = 1
    self:setHint(HINT_ADVANCE)
    if type(page.onShow) == "function" then
        page.onShow()
    end
    self:startReveal(page.text or "")
    self:presentChoices()
end

function Script:paintChoices()
    local choices = (self.page and self.page.choices) or {}
    if #choices > #self.choiceRows then
        log:warn("Dialogue: only " .. #self.choiceRows .. " choices fit on the right")
    end
    for i, row in ipairs(self.choiceRows) do
        local choice = choices[i]
        local label = self.choiceLabels[i]
        local mark = self.choiceMarks[i]
        if row and label and choice then
            -- The label string stays put. The chevron lives in a fixed-width
            -- slot, so moving the highlight does not resize the column.
            local selected = i == self.choiceIndex
            label.text = Locale.tr(choice.text or "")
            label.color = selected and COLOR_ON or COLOR_OFF
            if mark then
                mark.text = selected and ">" or ""
                mark.color = selected and COLOR_ON or COLOR_OFF
            end
            row.visible = true
        elseif row then
            if label then
                label.text = ""
            end
            if mark then
                mark.text = ""
            end
            row.visible = false
        end
    end
end

function Script:presentChoices()
    local choices = self.page and self.page.choices
    if type(choices) ~= "table" or #choices == 0 then
        self.bChoicesShown = false
        self:setHint(HINT_ADVANCE)
        self:paintChoices()
        return
    end
    self.bChoicesShown = true
    self:setHint(HINT_CHOOSE)
    self:paintChoices()
end

function Script:moveChoice(delta)
    local count = #self.page.choices
    if count == 0 then
        return
    end
    local index = self.choiceIndex + delta
    if index < 1 then
        index = count
    elseif index > count then
        index = 1
    end
    self.choiceIndex = index
    self:paintChoices()
end

-- `next` nil advances an array page, and closes a named page.
function Script:go(nxt)
    if nxt ~= nil then
        if self.script[nxt] == nil then
            log:warn("Dialogue: no page '" .. tostring(nxt) .. "'")
            self:close()
            return
        end
        self:showPage(nxt)
        return
    end
    if type(self.pageId) == "number" and self.pageId < #self.script then
        self:showPage(self.pageId + 1)
        return
    end
    self:close()
end

function Script:setHint(key)
    if self.hint then
        self.hint.text = Locale.tr(key)
    end
end

function Script:startReveal(text)
    self.pageText = text
    self.revealText = Locale.tr(text)
    self.pageCount = codePointCount(self.revealText)
    self.shown = 0
    self.body.text = ""
    if self.revealTimer then
        self.revealTimer:cancel()
    end
    self.revealTimer = self:every(REVEAL_SECONDS, function()
        self.shown = self.shown + 1
        self.body.text = string.sub(self.revealText, 1, codePointEnd(self.revealText, self.shown))
        if self.shown >= self.pageCount then
            self.revealTimer:cancel()
            self.revealTimer = nil
        end
    end)
end

function Script:applyLocale()
    if self.title then
        self.title.text = Locale.tr("Dialogue")
    end
    self:setHint(self.bChoicesShown and HINT_CHOOSE or HINT_ADVANCE)
    if self.pageText == nil or self.script == nil then
        return
    end
    if self.revealTimer then
        self:startReveal(self.pageText)
    else
        self.revealText = Locale.tr(self.pageText)
        self.pageCount = codePointCount(self.revealText)
        self.shown = self.pageCount
        self.body.text = self.revealText
    end
    if self.page then
        self:paintChoices()
    end
end

function Script:close()
    if self.revealTimer then
        self.revealTimer:cancel()
        self.revealTimer = nil
    end
    self.script = nil
    self.page = nil
    self.bChoicesShown = false
    local onDone = self.onDone
    self.onDone = nil
    self.root.visible = false
    self.body.text = ""
    self.pageText = nil
    self.revealText = nil
    self:paintChoices()
    self:setHint(HINT_ADVANCE)
    if onDone then
        onDone()
    end
end

function Script:onInit()
    self.box = self:find("Box")
    self.body = self:find("Body")
    self.hint = self:find("Hint")
    self.title = self:find("Title")
    self.choiceRows = {
        self:find("ChoiceRow1"),
        self:find("ChoiceRow2"),
        self:find("ChoiceRow3"),
        self:find("ChoiceRow4"),
    }
    self.choiceMarks = {
        self:find("Mark1"),
        self:find("Mark2"),
        self:find("Mark3"),
        self:find("Mark4"),
    }
    self.choiceLabels = {
        self:find("Choice1"),
        self:find("Choice2"),
        self:find("Choice3"),
        self:find("Choice4"),
    }
    self.script = nil
    self.page = nil
    self.pageId = nil
    self.bChoicesShown = false
    self.choiceIndex = 1
    self.shown = 0
    self.pageCount = 0
    self.pageText = nil
    self.revealText = nil
    self.revealTimer = nil
    self.onDone = nil
    self.openedFrame = -1
    self:applyLocale()
    self:setTickEnabled(true)
end

function Script:finishReveal()
    self.shown = self.pageCount
    self.body.text = self.revealText or ""
    if self.revealTimer then
        self.revealTimer:cancel()
        self.revealTimer = nil
    end
end

function Script:onUpdate(dt)
    if settingsBusy() or not self.script then
        return
    end
    -- The opening press (same frame as say) must not skip or advance.
    if time:getFrameIndex() == self.openedFrame then
        return
    end
    if self.bChoicesShown then
        if input:isActionPressed("move_up") then
            self:moveChoice(-1)
            return
        end
        if input:isActionPressed("move_down") then
            self:moveChoice(1)
            return
        end
    end
    if not confirmPressed() then
        return
    end
    if self.shown < self.pageCount then
        self:finishReveal()
        return
    end
    if self.bChoicesShown then
        local choice = self.page.choices[self.choiceIndex]
        self:go(choice and choice.next or nil)
        return
    end
    self:go(self.page and self.page.next or nil)
end

return Script
