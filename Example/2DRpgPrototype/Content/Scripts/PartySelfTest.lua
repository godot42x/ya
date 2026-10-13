-- PartySelfTest.lua -- G1: exercise the party growth rules in a running game
-- and log the results, so an automation run can assert them from the log.
--
-- It runs once, then disables itself. Everything it checks is the Party.lua
-- contract: exp -> level, level -> stats, hp carry-over, and that the roster
-- lives in Persist (so it survives a scene transfer).
local ScriptBase = require("ScriptBase")
local Party = require("Party")
local Script = ScriptBase:new()

function Script:onInit()
    self.done = false
end

local function report(tag, ok, detail)
    log:info(string.format("PartySelfTest %s %s %s", tag, ok and "OK" or "FAIL", detail or ""))
end

function Script:onUpdate(dt)
    if self.done then
        return
    end
    self.done = true

    -- Fresh member: level 1, stats equal the job's base.
    local m = Party.ensure("selftest", "knight", "Tester")
    local base = Party.stats("selftest")
    report("base_maxHp", base.maxHp == Party.jobs.knight.maxHp, tostring(base.maxHp))
    report("base_level", base.level == 1, tostring(base.level))

    -- Enough exp for exactly one level: expToNext(1) = 20.
    local need = Party.expToNext(1)
    local gained = Party.gainExp("selftest", need)
    report("level_up_once", #gained == 1 and gained[1].level == 2, tostring(#gained))

    -- Level 2 maxHp = base + one growth step.
    local lvl2 = Party.stats("selftest")
    report("level2_maxHp", lvl2.maxHp == Party.jobs.knight.maxHp + Party.jobs.knight.growth.hp,
           tostring(lvl2.maxHp))

    -- hp carry-over clamps to the new max.
    Party.setHp("selftest", 999)
    report("hp_clamped", Party.hp("selftest") == lvl2.maxHp, tostring(Party.hp("selftest")))

    -- The roster lives in Persist (survives a scene transfer).
    report("roster_in_persist", Persist.party ~= nil and Persist.party["selftest"] ~= nil, "")
end

return Script
