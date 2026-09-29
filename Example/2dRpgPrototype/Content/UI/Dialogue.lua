-- Dialogue.lua
-- Widget script of the Dialogue entry. Gameplay puts words on screen:
--
--   ui.get("Dialogue"):say(lines, onDone)
--
-- `lines` is an array of pages. The typewriter reveals the current page one
-- character at a time (self:every); confirm (Space/Enter/E) first reveals the
-- rest of the page, then advances, then closes and calls onDone. The frame
-- that opened the box ignores confirm, so the press that started the talk
-- never skips the first page. string.sub slices bytes: keep the pages ASCII
-- until a utf8-aware reveal is needed.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local REVEAL_SECONDS = 0.02
local CONFIRM_KEYS = { EKey.Space, EKey.Enter, EKey.K_E }

local function confirmPressed()
    for _, key in ipairs(CONFIRM_KEYS) do
        -- isKeyPressed is the down edge (isKeyDown is the held state).
        if input:isKeyPressed(key) then
            return true
        end
    end
    return false
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
    self.shown = 0
    self.body.text = ""
    if self.revealTimer then
        self.revealTimer:cancel()
    end
    self.revealTimer = self:every(REVEAL_SECONDS, function()
        self.shown = self.shown + 1
        self.body.text = string.sub(self.pageText, 1, self.shown)
        if self.shown >= #self.pageText then
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
    if self.shown < #self.pageText then
        -- First press: reveal the rest of the page at once.
        self.shown = #self.pageText
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
