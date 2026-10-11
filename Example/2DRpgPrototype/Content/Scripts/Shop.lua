-- One stall. Prices are coin counts. Buying spends coins from Persist.items
-- and gives the ware. A ware already in the bag is not sold again.
local Items = require("Items")

local Shop = {}

Shop.stock = {
    well_charm = { price = 3, coin = "coin" },
}

function Shop.price(id)
    local row = Shop.stock[id]
    return row and row.price or nil
end

-- "ok", "have", "poor", or "unknown".
function Shop.buy(id)
    local row = Shop.stock[id]
    if row == nil then
        log:warn("Shop.buy: unknown ware '" .. tostring(id) .. "'")
        return "unknown"
    end
    if Items.count(id) > 0 then
        return "have"
    end
    if not Items.take(row.coin, row.price) then
        return "poor"
    end
    Items.give(id, 1)
    return "ok"
end

return Shop
