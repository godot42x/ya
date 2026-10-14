-- Loot.lua -- what a won battle leaves behind (G2). A drop is one item from a
-- fixed table, added to the bag in `Persist.items`. The table and the roll
-- live here, not in Battle.lua, so the same rule that a victory uses is the one
-- a test exercises; Battle.lua only decides *when* a victory happens.
--
-- Only equipment drops this round (no consumables, no rarity, no durability):
-- the point is to feed the bag so the equipment screen has something to wear.
local Items = require("Items")

local Loot = {}

-- The default table a battle rolls from. One entry per equipment slot, so a
-- run tends to complete a set. Ids must be equippable (Items.slotFor ~= nil).
Loot.table = { "iron_sword", "leather_armor", "speed_ring" }

-- Roll one id from `table` (default Loot.table) -- an empty or nil table means
-- "the default table", since Lua's `{}` is truthy and a plain `or` fallback
-- would silently roll nothing. `index` pins a specific row when it is 1..N;
-- nil or <=0 rolls randomly. A test passes an index to make the roll
-- deterministic, production leaves it out. Returns the id, or nil for an empty
-- table.
function Loot.roll(table, index)
    if table == nil or #table == 0 then
        table = Loot.table
    end
    local count = #table
    if count == 0 then
        return nil
    end
    local pick = index
    if pick == nil or pick <= 0 then
        pick = math.random(count)
    end
    pick = math.max(1, math.min(count, pick))
    return table[pick]
end

-- Roll from `table` and put the result in the bag. Returns the dropped id (or
-- nil), and logs it so an automation run can read the drop back out of the log
-- without a second query surface.
function Loot.award(table, index)
    local id = Loot.roll(table, index)
    if id == nil then
        return nil
    end
    Items.give(id, 1)
    log:info(string.format("Battle: loot %s x1", id))
    return id
end

return Loot
