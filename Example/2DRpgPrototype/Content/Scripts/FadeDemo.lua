-- FadeDemo.lua -- R1 smoke: drive the FadeOverlay entry from gameplay, the way
-- a scene transition would. It fades to black, holds, then fades back, on a
-- loop, so a screenshot at a known moment shows a known overlay alpha.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local HOLD_ON_BLACK = 2.5
local FADE_SECONDS = 0.5

function Script:onInit()
    self.t = 0
    self.phase = "clear"
    log:info("FadeDemo: start")
end

function Script:onUpdate(dt)
    self.t = self.t + dt
    local overlay = ui.get("FadeOverlay")
    if type(overlay) ~= "table" or overlay.fadeTo == nil then
        return
    end
    if self.phase == "clear" and self.t >= 2.0 then
        self.phase = "out"
        self.t = 0
        overlay:fadeOut(FADE_SECONDS)
        log:info("FadeDemo: fade out")
    elseif self.phase == "out" and self.t >= FADE_SECONDS + HOLD_ON_BLACK then
        self.phase = "in"
        self.t = 0
        overlay:fadeIn(FADE_SECONDS)
        log:info("FadeDemo: fade in")
    elseif self.phase == "in" and self.t >= FADE_SECONDS + 1.0 then
        self.phase = "done"
        log:info("FadeDemo: done")
    end
end

return Script
