-- Party.lua -- the party roster and its growth, as data. Counts and levels live
-- in `Persist.party`, so a scene transfer keeps them and a save keeps them, the
-- same rule Items/Quests follow.
--
-- A member is `{ displayName, level, exp, job, hp, maxHp, ... }`; the battle
-- reads a member's stats to build its unit, and writes hp/xp back on the
-- outcome. This module owns the growth rules; Battle.lua owns the fight.
---@class Party
local Party = {}

-- Job tables: base stats and how they grow per level. A member's job decides
-- its skill list (see `Skills.lua`).
Party.jobs = {
    knight = { title = "Knight", maxHp = 30, atk = 7, def = 3, spd = 8, move = 3, range = 1,
               growth = { hp = 4, atk = 2, def = 1, spd = 1 } },
    archer = { title = "Archer", maxHp = 20, atk = 6, def = 1, spd = 12, move = 3, range = 3,
               growth = { hp = 3, atk = 2, def = 1, spd = 1 } },
    mage   = { title = "Mage", maxHp = 18, atk = 5, def = 0, spd = 10, move = 2, range = 2,
               growth = { hp = 2, atk = 3, def = 1, spd = 1 } },
}

-- Exp needed to reach the next level from `level`. Simple and readable; a curve
-- can replace it without touching callers.
function Party.expToNext(level)
    return 20 + (level - 1) * 15
end

-- The roster table, created on first use.
function Party.roster()
    Persist.party = Persist.party or {}
    return Persist.party
end

-- Register (or return) a member by id; the battle calls this on scene init to
-- make sure the roster has the units the scene authored.
function Party.ensure(id, job, displayName)
    local roster = Party.roster()
    if roster[id] == nil then
        local j = Party.jobs[job] or Party.jobs.knight
        roster[id] = {
            displayName = displayName or j.title,
            job = job,
            level = 1,
            exp = 0,
            hp = j.maxHp,
        }
    end
    return roster[id]
end

-- A member's effective stats at its current level: base + growth * (level - 1).
function Party.stats(id)
    local member = Party.roster()[id]
    if member == nil then
        return nil
    end
    local j = Party.jobs[member.job] or Party.jobs.knight
    local n = member.level - 1
    return {
        displayName = member.displayName,
        maxHp = j.maxHp + j.growth.hp * n,
        atk   = j.atk + j.growth.atk * n,
        def   = j.def + j.growth.def * n,
        spd   = j.spd + j.growth.spd * n,
        move  = j.move,
        range = j.range,
        level = member.level,
    }
end

-- Grant exp and level up as many times as it allows. Returns a list of
-- `{ id, level }` for every level gained, so the caller can show the level-up.
function Party.gainExp(id, amount)
    local member = Party.roster()[id]
    if member == nil or amount <= 0 then
        return {}
    end
    member.exp = member.exp + amount
    local gained = {}
    while member.exp >= Party.expToNext(member.level) do
        member.exp = member.exp - Party.expToNext(member.level)
        member.level = member.level + 1
        gained[#gained + 1] = { id = id, level = member.level }
    end
    return gained
end

-- Carry a member's hp out of a battle (clamped to its max at the current level).
function Party.setHp(id, hp)
    local member = Party.roster()[id]
    if member == nil then
        return
    end
    local stats = Party.stats(id)
    member.hp = math.max(0, math.min(hp, stats and stats.maxHp or hp))
end

function Party.hp(id)
    local member = Party.roster()[id]
    return member and member.hp or 0
end

-- Bring every member to full (a rest at camp).
function Party.healAll()
    for id in pairs(Party.roster()) do
        local stats = Party.stats(id)
        if stats then
            Party.roster()[id].hp = stats.maxHp
        end
    end
end

return Party
