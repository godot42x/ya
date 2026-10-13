-- AudioDemo.lua -- A1 smoke: the audio subsystem from gameplay. It plays a UI
-- click when confirm is pressed and keeps a music track looping, so the log
-- shows the device opened, the clip decoded and the voice started.
--
-- Guarded by `audio.available` so a headless run (no device) is silent, not an
-- error. The engine's audio calls are already no-ops without a device.
local ScriptBase = require("ScriptBase")
local Script = ScriptBase:new()

Script.musicClip = "Content/Audio/SFX/door_open.ogg"
Script.clickClip = "Content/Audio/UI/click.ogg"
Script:properties({
    musicClip = { type = "string", tooltip = "Music track to loop; empty = none" },
    clickClip = { type = "string", tooltip = "Sound played on confirm" },
})

function Script:onInit()
    self.started = false
    self.bArmed = true
end

function Script:onUpdate(dt)
    if not self.started then
        self.started = true
        if self.musicClip ~= "" then
            audio.music(self.musicClip, 0.0)
        end
        log:info("AudioDemo: music requested")
    end
    if not input:isActionDown("confirm") then
        self.bArmed = true
    end
    if self.bArmed and input:isActionPressed("confirm") then
        self.bArmed = false
        audio.play(self.clickClip, { bus = "ui", volume = 0.8 })
    end
end

return Script
