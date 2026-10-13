-- LookDemo.lua -- R1 smoke: drive post-processing from gameplay. It cycles a
-- few looks on a timer so a screenshot at a known moment shows a known grade.
--
-- Looks, in order:
--   1 (0.0s-1.2s)  neutral          (tone-map + gamma, no grade)
--   2 (1.2s-2.4s)  heavy exposure   (bright)
--   3 (2.4s-3.6s)  grayscale        (desaturated)
--   4 (3.6s-4.8s)  bloom            (glow threshold lowered)
-- It holds the last look afterwards.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

local LOOKS = {
    { at = 0.0, look = { exposure = 0.6, bBloom = false, grayscale = 0, bToneMap = true, bGamma = true } },
    { at = 1.2, look = { exposure = 2.2, bBloom = false, grayscale = 0, bToneMap = true, bGamma = true } },
    { at = 2.4, look = { exposure = 0.6, bBloom = false, grayscale = 2, bToneMap = true, bGamma = true } },
    { at = 3.6, look = { exposure = 1.4, bBloom = true, bloomThreshold = 0.35, bloomStrength = 1.2,
                         grayscale = 0, bToneMap = true, bGamma = true } },
}

function Script:onInit()
    self.t = 0
    self.index = 0
end

function Script:onUpdate(dt)
    self.t = self.t + dt
    local next_index = self.index
    for i, entry in ipairs(LOOKS) do
        if self.t >= entry.at then
            next_index = i
        end
    end
    if next_index ~= self.index then
        self.index = next_index
        render.setLook(LOOKS[self.index].look)
        log:info("LookDemo: look " .. self.index)
    end
end

return Script
