-- EquipmentSelfTest.lua -- G2: exercise the equipment/inventory contract in a
-- running game and log one line per check, so an automation run can assert them
-- from the log (the PartySelfTest pattern). It runs once, then disables itself.
--
-- It checks the rules the whole G2 feature rests on:
--   * equipping a weapon raises atk by its bonus, unequipping restores base;
--   * equipping moves the item out of the bag, unequipping puts it back;
--   * an armor's maxHp bonus folds into the stats (base + level + equipment);
--   * a non-equipment item has no slot and cannot be equipped;
--   * the loot table only drops equippable ids, and a pinned roll is exact.
--
-- It leaves a known loadout behind for the UI phase of the test (open the
-- panel, equip a weapon): one of each equipment in the bag, nothing equipped.
local ScriptBase = require("ScriptBase")
local Items = require("Items")
local Party = require("Party")
local Loot = require("Loot")
local Script = ScriptBase:new()

local ROSTER = { "archer", "knight" }

local function report(tag, ok, detail)
    log:info(string.format("EquipmentSelfTest %s %s %s", tag, ok and "OK" or "FAIL", detail or ""))
end

-- One of each equipment in the bag, nothing equipped on either member.
local function resetLoadout()
    Persist.items = {}
    for _, id in ipairs(Items.SLOTS) do
        for candidate, item in pairs(Items.catalog) do
            if item.type == id then
                Items.give(candidate, 1)
            end
        end
    end
    for _, memberId in ipairs(ROSTER) do
        Party.ensure(memberId, memberId, memberId).equipment = {}
    end
end

function Script:onInit()
    self.done = false
end

function Script:onUpdate(dt)
    if self.done then
        return
    end
    self.done = true

    resetLoadout()

    -- Equipping raises atk by the item's bonus; unequipping restores base.
    local base = Party.stats("archer")
    Party.equip("archer", "iron_sword")
    local armed = Party.stats("archer")
    report("equip_raises_atk", armed.atk == base.atk + Items.get("iron_sword").stats.atk,
           string.format("%d -> %d", base.atk, armed.atk))

    -- The equipped item left the bag and sits in the slot.
    report("equip_consumes_bag", Items.count("iron_sword") == 0, tostring(Items.count("iron_sword")))
    report("slot_holds_item", Party.equipped("archer", "weapon") == "iron_sword",
           tostring(Party.equipped("archer", "weapon")))

    Party.unequip("archer", "weapon")
    report("unequip_restores_base", Party.stats("archer").atk == base.atk,
           string.format("%d -> %d", armed.atk, Party.stats("archer").atk))
    report("unequip_returns_to_bag", Items.count("iron_sword") == 1, tostring(Items.count("iron_sword")))

    -- An armor's maxHp folds into the stats and back out.
    local armorBase = Party.stats("archer").maxHp
    Party.equip("archer", "leather_armor")
    report("armor_adds_maxhp",
           Party.stats("archer").maxHp == armorBase + Items.get("leather_armor").stats.maxHp,
           string.format("%d -> %d", armorBase, Party.stats("archer").maxHp))
    Party.unequip("archer", "armor")
    report("armor_removed_restores_maxhp", Party.stats("archer").maxHp == armorBase,
           tostring(Party.stats("archer").maxHp))

    -- A non-equipment item has no slot.
    report("coin_is_not_equippable", Items.slotFor("coin") == nil, "")
    report("equip_rejects_non_equipment", Party.equip("archer", "coin") == "not_equippable",
           tostring(Party.equip("archer", "coin")))

    -- Only equippable ids can drop, and a pinned roll is exact.
    local allEquippable = true
    for _, id in ipairs(Loot.table) do
        if not Items.isEquippable(id) then
            allEquippable = false
        end
    end
    report("loot_table_is_equippable", allEquippable, "")
    report("loot_roll_pins_row", Loot.roll(Loot.table, 2) == Loot.table[2],
           tostring(Loot.roll(Loot.table, 2)))
    local before = Items.count(Loot.table[1])
    local dropped = Loot.award(Loot.table, 1)
    report("loot_award_adds_to_bag",
           dropped == Loot.table[1] and Items.count(Loot.table[1]) == before + 1,
           string.format("%s now %d", tostring(dropped), Items.count(Loot.table[1])))

    resetLoadout()
    report("ready", true, "")
end

return Script
