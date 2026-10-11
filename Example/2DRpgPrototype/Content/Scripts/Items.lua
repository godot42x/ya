-- Item names live here. Counts live in Persist.items, so a save keeps
-- them and a scene transfer does not clear them. Only ids in the catalog
-- can be given.
local Locale = require("Locale")
local Items = {}

Items.catalog = {
    brass_key = "Brass Key",
    coin = "Coin",
    well_charm = "Well Charm",
    well_glass = "Well Glass",
}

function Items.count(id)
    local items = Persist.items
    if type(items) ~= "table" then
        return 0
    end
    return items[id] or 0
end

function Items.give(id, count)
    local name = Items.catalog[id]
    if name == nil then
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

function Items.lines()
    local items = Persist.items
    if type(items) ~= "table" then
        return {}
    end
    local lines = {}
    for id, count in pairs(items) do
        local name = Items.catalog[id] or id
        lines[#lines + 1] = Locale.tr(name) .. " x" .. tostring(count)
    end
    table.sort(lines)
    return lines
end

return Items
