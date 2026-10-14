-- Party.lua -- the party roster and its growth, as data. Counts and levels live
-- in `Persist.party`, so a scene transfer keeps them and a save keeps them, the
-- same rule Items/Quests follow.
--
-- A member is `{ displayName, level, exp, job, hp, maxHp, ... }`; the battle
-- reads a member's stats to build its unit, and writes hp/xp back on the
-- outcome. This module owns the growth rules; Battle.lua owns the fight.
--
-- A member also carries `equipment = { weapon=?, armor=?, accessory=? }` (G2):
-- one item id per slot, or nil. `Party.stats` folds base + level growth +
-- equipment, so every caller that asks for stats sees the equipped totals and
-- nothing keeps a second stat copy. Items.lua owns the catalog; Party owns who
-- wears what. An equipped item lives in the slot, not in the bag, and swapping
-- returns the old one to the bag.
---@class Party
local Party = {}

local Items = require("Items")

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

-- The member ids in a stable (sorted) order. A raw `pairs` walk of the roster
-- is not deterministic, and the equipment panel has to cycle members the same
-- way twice.
function Party.memberIds()
    local ids = {}
    for id in pairs(Party.roster()) do
        ids[#ids + 1] = id
    end
    table.sort(ids)
    return ids
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
            equipment = {},
        }
    end
    -- A member from an older save has no equipment table yet.
    roster[id].equipment = roster[id].equipment or {}
    return roster[id]
end

-- ── equipment (G2) ────────────────────────────────────────────────────────

-- The member's equipped items, one id per slot. Always a table, so callers can
-- index it without a nil check.
function Party.getEquipment(id)
    local member = Party.roster()[id]
    if member == nil then
        return nil
    end
    member.equipment = member.equipment or {}
    return member.equipment
end

-- Fold an item's flat stat bonus into a stat table. A stat the item does not
-- carry is left alone.
local function addItemStats(stats, itemId)
    local item = Items.get(itemId)
    if item == nil or item.stats == nil then
        return
    end
    for stat, value in pairs(item.stats) do
        stats[stat] = (stats[stat] or 0) + value
    end
end

-- Equip `itemId` into its slot (its `type`). Returns "ok", "unknown",
-- "not_equippable", "not_owned", or "already". The item leaves the bag; an item
-- already in that slot goes back to the bag. hp is not refilled by the swap
-- (only the max changes), so equipping mid-run cannot heal.
function Party.equip(id, itemId)
    local member = Party.roster()[id]
    if member == nil then
        return "unknown"
    end
    local slot = Items.slotFor(itemId)
    if slot == nil then
        return Items.get(itemId) and "not_equippable" or "unknown"
    end
    member.equipment = member.equipment or {}
    if member.equipment[slot] == itemId then
        return "already"
    end
    if Items.count(itemId) < 1 then
        return "not_owned"
    end
    Items.take(itemId, 1)
    if member.equipment[slot] then
        Items.give(member.equipment[slot], 1)
    end
    member.equipment[slot] = itemId
    return "ok"
end

-- Take the item out of `slot` and put it back in the bag. Returns the item id
-- that was unequipped, or nil when the slot was empty.
function Party.unequip(id, slot)
    local member = Party.roster()[id]
    if member == nil or member.equipment == nil then
        return nil
    end
    local itemId = member.equipment[slot]
    if itemId == nil then
        return nil
    end
    member.equipment[slot] = nil
    Items.give(itemId, 1)
    return itemId
end

function Party.equipped(id, slot)
    local member = Party.roster()[id]
    return member and member.equipment and member.equipment[slot] or nil
end

-- A member's effective stats: base + level growth + equipment.
function Party.stats(id)
    local member = Party.roster()[id]
    if member == nil then
        return nil
    end
    local j = Party.jobs[member.job] or Party.jobs.knight
    local n = member.level - 1
    local stats = {
        displayName = member.displayName,
        maxHp = j.maxHp + j.growth.hp * n,
        atk   = j.atk + j.growth.atk * n,
        def   = j.def + j.growth.def * n,
        spd   = j.spd + j.growth.spd * n,
        move  = j.move,
        range = j.range,
        level = member.level,
    }
    for _, itemId in pairs(member.equipment or {}) do
        if itemId then
            addItemStats(stats, itemId)
        end
    end
    return stats
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
