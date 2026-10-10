-- Dialogue.lua
-- Widget script of the Dialogue entry. Gameplay puts words on screen:
--
--   ui.get("Dialogue"):say(lines, onDone)
--
-- `lines` is an array of pages. The typewriter writes a code-point prefix of
-- the current page into Body.text (self:every). Confirm first shows the rest
-- of the page, then advances, then closes and calls onDone. The frame that
-- opened the box ignores confirm, so the press that started the talk never
-- skips the first page.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local REVEAL_SECONDS = 0.02

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

-- True while a dialogue is on screen; the player script holds movement by
-- polling this (game-ui S4 modal entries will take over that hold).
function Script:busy()
    return self.lines ~= nil
end

function Script:say(lines, onDone)
    if type(lines) ~= "table" or #lines == 0 then
        log:warn("Dialogue:say expects a non-empty array of lines")
        return
    end
    self.lines = lines
    self.lineIndex = 1
    self.onDone = onDone
    self.openedFrame = time:getFrameIndex()
    self.root.visible = true
    self:startReveal(lines[1])
end

function Script:startReveal(text)
    self.pageText = text
    self.pageCount = codePointCount(text)
    self.shown = 0
    self.body.text = ""
    if self.revealTimer then
        self.revealTimer:cancel()
    end
    self.revealTimer = self:every(REVEAL_SECONDS, function()
        self.shown = self.shown + 1
        self.body.text = string.sub(self.pageText, 1, codePointEnd(self.pageText, self.shown))
        if self.shown >= self.pageCount then
            self.revealTimer:cancel()
            self.revealTimer = nil
        end
    end)
end

function Script:close()
    if self.revealTimer then
        self.revealTimer:cancel()
        self.revealTimer = nil
    end
    self.lines = nil
    local onDone = self.onDone
    self.onDone = nil
    self.root.visible = false
    self.body.text = ""
    if onDone then
        onDone()
    end
end

function Script:onInit()
    self.box = self:find("Box")
    self.body = self:find("Body")
    self.lines = nil
    self.lineIndex = 0
    self.shown = 0
    self.pageCount = 0
    self.pageText = nil
    self.revealTimer = nil
    self.onDone = nil
    self.openedFrame = -1
    self:setTickEnabled(true)
end

function Script:onUpdate(dt)
    if not self.lines then
        return
    end
    -- The opening press (same frame as say) must not skip or advance.
    if time:getFrameIndex() == self.openedFrame or not confirmPressed() then
        return
    end
    if self.shown < self.pageCount then
        -- First press: reveal the rest of the page at once.
        self.shown = self.pageCount
        self.body.text = self.pageText
        self.revealTimer:cancel()
        self.revealTimer = nil
        return
    end
    if self.lineIndex < #self.lines then
        self.lineIndex = self.lineIndex + 1
        self:startReveal(self.lines[self.lineIndex])
        return
    end
    self:close()
end

return Script
