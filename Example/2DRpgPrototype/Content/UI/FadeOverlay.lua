-- FadeOverlay.lua -- a full-screen tint used for transitions. Other scripts
-- reach it with `ui.get("FadeOverlay")`:
--
--   overlay:fadeOut(0.4, {0,0,0})            -- to black over 0.4s, then hold
--   overlay:fadeIn(0.4)                      -- back to clear
--   overlay:setAlpha(0.5)                    -- jump
--   overlay:isFading()
--
-- It owns one Border ("Shade") that fills the viewport; only its alpha moves.
-- A "cover then swap the scene then uncover" transition is:
--   overlay:fadeOut(0.3); scene.load...; overlay:fadeIn(0.3)
-- The tween runs on the widget's own update clock, so it keeps advancing while
-- the game is otherwise idle.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local function colorWith(alpha, rgb)
    return Vec4.new(rgb[1], rgb[2], rgb[3], alpha)
end

function Script:setShade(alpha)
    self.alpha = math.max(0.0, math.min(1.0, alpha))
    if self.shade then
        -- A Border's script field is `fill` (its reflected member is _color).
        self.shade.fill = colorWith(self.alpha, self.rgb)
    end
end

function Script:setAlpha(alpha)
    self.duration = 0
    self.from = self.alpha or 0.0
    self.to = alpha
    self.elapsed = 0
    self:setShade(alpha)
end

-- `rgb` is an optional {r, g, b} table; default black.
function Script:fadeTo(target, seconds, rgb)
    if rgb then
        self.rgb = rgb
    end
    self.from = self.alpha or 0.0
    self.to = math.max(0.0, math.min(1.0, target))
    self.duration = math.max(0.0, seconds or 0.3)
    self.elapsed = 0
    if self.duration <= 0 then
        self:setShade(self.to)
        self.from = self.to
    end
end

function Script:fadeOut(seconds, rgb)
    self:fadeTo(1.0, seconds, rgb)
end

function Script:fadeIn(seconds)
    self:fadeTo(0.0, seconds)
end

function Script:isFading()
    return self.duration > 0 and self.elapsed < self.duration
end

function Script:onInit()
    self.shade = self:find("Shade")
    self.alpha = 0.0
    self.from = 0.0
    self.to = 0.0
    self.elapsed = 0
    self.duration = 0
    self.rgb = { 0.0, 0.0, 0.0 }
    -- Arrive covered and uncover: a scene that was entered through a fade lands
    -- on black and fades in, so the player never sees a hard cut. A scene loaded
    -- without a fade still starts covered, so it fades in from black; set
    -- `introSeconds = 0` in the .yaui to have the overlay start clear instead.
    self:setShade(1.0)
    self.alpha = 1.0
    self:fadeIn(self.introSeconds or 0.35)
    self:setTickEnabled(true)
end

function Script:onUpdate(dt)
    if not self:isFading() then
        return
    end
    self.elapsed = self.elapsed + dt
    local k = self.elapsed / self.duration
    if k >= 1.0 then
        self.elapsed = self.duration
        self:setShade(self.to)
        return
    end
    self:setShade(self.from + (self.to - self.from) * k)
end

return Script
