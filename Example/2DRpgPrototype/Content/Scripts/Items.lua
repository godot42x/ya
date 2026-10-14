-- Items.lua -- the item catalog and the bag. Counts live in `Persist.items`,
-- so a save keeps them and a scene transfer does not clear them.
--
-- A catalog entry is a definition, not a name:
--   { id, type, name, stats?, desc? }
-- `type` is either an equipment slot ("weapon" / "armor" / "accessory") or a
-- non-equippable kind ("key" / "coin"). `stats` is the flat bonus an equipped
-- item adds; there is no durability or upgrade this round, only "owned /
-- equipped" and a stat number (G2).
--
-- The bag API keeps the names the landed content already uses (`give` / `take`
-- / `count` / `lines`); `addToInventory` / `removeFromInventory` /
-- `getInventory` are the same functions under the names G2's spec used.
local Locale = require("Locale")
local Items = {}

-- Equipment slots, in display order. An item's slot is its `type`, so the two
-- never drift.
Items.SLOTS = { "weapon", "armor", "accessory" }

-- How a stat is written back to the player.
Items.STAT_LABELS = { maxHp = "HP", atk = "ATK", def = "DEF", spd = "SPD",
                      move = "MOVE", range = "RANGE" }
Items.STAT_ORDER = { "maxHp", "atk", "def", "spd", "move", "range" }

Items.catalog = {
    -- Quest goods carried by the v1 town loop.
    brass_key  = { id = "brass_key",  type = "key",  name = "Brass Key" },
    coin       = { id = "coin",       type = "coin", name = "Coin" },
    well_charm = { id = "well_charm", type = "key",  name = "Well Charm" },
    well_glass = { id = "well_glass", type = "key",  name = "Well Glass" },

    -- Equipment (G2). One entry per slot; battles drop from these.
    iron_sword = {
        id = "iron_sword", type = "weapon", name = "Iron Sword",
        stats = { atk = 3 }, desc = "A plain soldier's blade, still straight.",
    },
    leather_armor = {
        id = "leather_armor", type = "armor", name = "Leather Armor",
        stats = { def = 2, maxHp = 5 }, desc = "Boiled leather. Worn, but it holds.",
    },
    speed_ring = {
        id = "speed_ring", type = "accessory", name = "Speed Ring",
        stats = { spd = 2 }, desc = "Warm to the touch. It hums before danger.",
    },
}

function Items.get(id)
    return Items.catalog[id]
end

function Items.name(id)
    local item = Items.catalog[id]
    return item and item.name or tostring(id)
end

-- The slot an item equips into, or nil when it is not wearable.
function Items.slotFor(id)
    local item = Items.catalog[id]
    if item == nil then
        return nil
    end
    for _, slot in ipairs(Items.SLOTS) do
        if item.type == slot then
            return slot
        end
    end
    return nil
end

function Items.isEquippable(id)
    return Items.slotFor(id) ~= nil
end

-- "ATK +3  DEF +2" -- the bonus line the panels show.
function Items.statText(id)
    local item = Items.catalog[id]
    if item == nil or item.stats == nil then
        return ""
    end
    local parts = {}
    for _, stat in ipairs(Items.STAT_ORDER) do
        local value = item.stats[stat]
        if value then
            parts[#parts + 1] = string.format("%s +%d", Items.STAT_LABELS[stat] or stat, value)
        end
    end
    return table.concat(parts, "  ")
end

function Items.count(id)
    local items = Persist.items
    if type(items) ~= "table" then
        return 0
    end
    return items[id] or 0
end

function Items.give(id, count)
    if Items.catalog[id] == nil then
        log:warn("Items.give: unknown item '" .. tostring(id) .. "'")
        return
    end
    Persist.items = Persist.items or {}
    Persist.items[id] = (Persist.items[id] or 0) + (count or 1)
end

-- False when the bag does not hold enough. A spent stack is removed.
function Items.take(id, count)
    count = count or 1
    if Items.count(id) < count then
        return false
    end
    local left = Persist.items[id] - count
    if left <= 0 then
        Persist.items[id] = nil
    else
        Persist.items[id] = left
    end
    return true
end

-- The bag as rows, sorted by id so the panel reads the same way twice.
function Items.inventory()
    local items = Persist.items
    if type(items) ~= "table" then
        return {}
    end
    local rows = {}
    for id, count in pairs(items) do
        rows[#rows + 1] = { id = id, count = count }
    end
    table.sort(rows, function(a, b) return a.id < b.id end)
    return rows
end

function Items.lines()
    local lines = {}
    for _, row in ipairs(Items.inventory()) do
        lines[#lines + 1] = Locale.tr(Items.name(row.id)) .. " x" .. tostring(row.count)
    end
    return lines
end

-- The names this checkpoint's spec used, pointing at the same storage.
Items.addToInventory = Items.give
Items.removeFromInventory = Items.take
Items.getInventory = Items.inventory

return Items
