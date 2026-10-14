-- Inventory.lua -- the bag and equipment screen (G2). Widget script of the
-- Inventory entry; I toggles it, and while it is up it owns the screen
-- (Player.lua freezes movement on busy()).
--
-- Layers:
--   bag rows   -> Items.inventory()      (id + count, sorted by id)
--   gear slots -> Party.getEquipment(id) (weapon / armor / accessory)
--   stats line -> Party.stats(id)        (base + level growth + equipment)
--   detail     -> the selected item's name / bonus / description
--
-- It owns no game state: every row is redrawn from Items and Party, so an
-- equip through a button and an equip through Party.equip() cannot drift. The
-- only state this script keeps is selection (which bag row, which member) --
-- view state, not game state.
--
-- Keys: Up / Down select a bag row, Left / Right pick the member, confirm
-- equips the selection. The buttons do the same. Unequip drops that slot's
-- item back into the bag and moves the selection onto it, so "unequip then
-- equip" is a round trip.
local ScriptBase = require("ScriptBase")
local Items = require("Items")
local Party = require("Party")
local Locale = require("Locale")
local Script = ScriptBase:new()

local BAG_ROWS = 6
local COLOR_ON = Vec4.new(0.96, 0.86, 0.48, 1.0)
local COLOR_OFF = Vec4.new(0.92, 0.92, 0.94, 1.0)
local COLOR_EMPTY = Vec4.new(0.55, 0.57, 0.62, 1.0)

local SLOT_WIDGET = { weapon = "SlotWeapon", armor = "SlotArmor", accessory = "SlotAccessory" }

local function dialogueBusy()
    local dialogue = ui.get("Dialogue")
    return type(dialogue) == "table" and dialogue.busy ~= nil and dialogue:busy()
end

local function settingsBusy()
    local settings = ui.get("Settings")
    return type(settings) == "table" and settings.busy ~= nil and settings:busy()
end

function Script:busy()
    return self.bOpen == true
end

-- ── model helpers ─────────────────────────────────────────────────────────

function Script:memberId()
    local ids = Party.memberIds()
    if #ids == 0 then
        return nil
    end
    if self.memberIndex < 1 or self.memberIndex > #ids then
        self.memberIndex = 1
    end
    return ids[self.memberIndex]
end

function Script:selectedRow()
    local rows = self.rows or {}
    local row = rows[self.bagIndex]
    if row == nil then
        self.bagIndex = 1
        row = rows[1]
    end
    return row
end

-- The slot the Unequip button acts on: the selected bag row's own slot when it
-- is filled (so selecting the armor and pressing Unequip takes the armor off),
-- otherwise the first filled slot. Always a filled slot or nil, so the button
-- is never enabled with nothing to do -- after an equip the selection slides to
-- the next bag row, and a rule that only looked at that row would leave the
-- weapon stuck on with the button pointed at an empty armor slot.
function Script:unequipTarget()
    local id = self:memberId()
    if id == nil then
        return nil
    end
    local equipped = Party.getEquipment(id)
    local row = self:selectedRow()
    local selectedSlot = row and Items.slotFor(row.id) or nil
    if selectedSlot and equipped[selectedSlot] then
        return selectedSlot
    end
    for _, slot in ipairs(Items.SLOTS) do
        if equipped[slot] then
            return slot
        end
    end
    return nil
end

-- ── actions ───────────────────────────────────────────────────────────────

function Script:doEquip()
    local id = self:memberId()
    local row = self:selectedRow()
    if id == nil or row == nil then
        return
    end
    local result = Party.equip(id, row.id)
    -- "ok"/"already" need no words: the slot and the stats line already show
    -- the outcome. A refusal says why (nothing/not wearable).
    if result == "ok" or result == "already" then
        self.notice = nil
    else
        self.notice = result
    end
    self:refresh()
end

function Script:doUnequip(slot)
    local id = self:memberId()
    if id == nil then
        return
    end
    local dropped = Party.unequip(id, slot or self:unequipTarget())
    if dropped ~= nil then
        -- Select the item that just came back, so equip re-arms it.
        self:refresh()
        for i, row in ipairs(self.rows or {}) do
            if row.id == dropped then
                self.bagIndex = i
                break
            end
        end
    end
    self:refresh()
end

function Script:cycleMember(delta)
    local ids = Party.memberIds()
    if #ids == 0 then
        return
    end
    self.memberIndex = self.memberIndex + delta
    if self.memberIndex < 1 then
        self.memberIndex = #ids
    elseif self.memberIndex > #ids then
        self.memberIndex = 1
    end
    self.notice = nil
    self:refresh()
end

function Script:moveSelection(delta)
    local count = #(self.rows or {})
    if count == 0 then
        return
    end
    self.bagIndex = self.bagIndex + delta
    if self.bagIndex < 1 then
        self.bagIndex = count
    elseif self.bagIndex > count then
        self.bagIndex = 1
    end
    self.notice = nil
    self:refresh()
end

-- ── paint ─────────────────────────────────────────────────────────────────

function Script:paintRows()
    for i, row in ipairs(self.bagRows or {}) do
        local entry = (self.rows or {})[i]
        if entry then
            local label = Locale.tr(Items.name(entry.id))
            if entry.count > 1 then
                label = string.format("%s x%d", label, entry.count)
            end
            local selected = i == self.bagIndex
            row.label.text = (selected and "> " or "  ") .. label
            -- The selection is the cursor: brighten it. The slot it would fill
            -- echoes it, so the Equip button's effect is visible before it runs.
            row.label.color = selected and COLOR_ON or COLOR_OFF
            row.button.visible = true
        else
            row.label.text = ""
            row.button.visible = false
        end
    end
end

function Script:paintSlots()
    local id = self:memberId()
    local equipped = id and Party.getEquipment(id) or {}
    for _, slot in ipairs(Items.SLOTS) do
        local row = self.slotButtons[slot]
        if row then
            local itemId = equipped[slot]
            row.label.text = itemId and Locale.tr(Items.name(itemId)) or "-"
            row.label.color = itemId and COLOR_OFF or COLOR_EMPTY
            -- A filled slot is the click-to-unequip target; an empty one is inert.
            row.button.enabled = itemId ~= nil
        end
    end
end

function Script:paintStats()
    local id = self:memberId()
    if id == nil then
        self.memberLabel.text = "-"
        self.statsText.text = "-"
        return
    end
    local member = Party.roster()[id]
    local stats = Party.stats(id)
    self.memberLabel.text = string.format("%s  Lv %d", Locale.tr(member.displayName), stats.level)
    self.statsText.text = string.format(
        "HP %d  ATK %d  DEF %d  SPD %d", stats.maxHp, stats.atk, stats.def, stats.spd)
end

function Script:paintDetail()
    local row = self:selectedRow()
    if row == nil then
        self.detailName.text = Locale.tr("Nothing carried.")
        self.detailStats.text = ""
        self.detailDesc.text = ""
        return
    end
    local item = Items.get(row.id)
    self.detailName.text = Locale.tr(Items.name(row.id))
    self.detailStats.text = Items.isEquippable(row.id) and Items.statText(row.id) or ""
    self.detailDesc.text = (item and item.desc) and Locale.tr(item.desc) or ""
end

-- The header line: the hotkey by default, a refusal reason while one is set.
-- Party.equip returns contract codes ("not_owned" ...); the panel owns how they
-- read. New codes fall back to the code itself rather than a silent blank.
local NOTICE_TEXT = {
    not_owned = "Not in the bag",
    not_equippable = "Cannot be equipped",
    unknown = "Unknown item",
}

function Script:paintHint()
    if self.hint == nil then
        return
    end
    self.hint.text = self.notice and Locale.tr(NOTICE_TEXT[self.notice] or self.notice) or "I"
end

function Script:refresh()
    self.rows = Items.inventory()
    self:paintRows()
    self:paintSlots()
    self:paintStats()
    self:paintDetail()
    self:paintHint()
    if self.equipButton then
        local row = self:selectedRow()
        self.equipButton.button.enabled = row ~= nil and Items.isEquippable(row.id)
    end
    if self.unequipButton then
        self.unequipButton.button.enabled = self:unequipTarget() ~= nil
    end
end

function Script:applyLocale()
    if self.title then
        self.title.text = Locale.tr("Inventory")
    end
    if self.equipButton then
        self.equipButton.label.text = Locale.tr("Equip")
    end
    if self.unequipButton then
        self.unequipButton.label.text = Locale.tr("Unequip")
    end
    if self.bOpen then
        self:refresh()
    end
end

-- ── lifecycle ─────────────────────────────────────────────────────────────

function Script:open()
    self.bOpen = true
    self.root.visible = true
    -- The press that opened the panel must not also be read as confirm, so
    -- the first tick after opening ignores input (Dialogue's openedFrame).
    self.openedFrame = time:getFrameIndex()
    self:refresh()
end

function Script:close()
    self.bOpen = false
    self.root.visible = false
end

function Script:toggle()
    if self.bOpen then
        self:close()
    else
        self:open()
    end
end

function Script:onCancel()
    if not self.bOpen then
        return false
    end
    self:close()
    return true
end

function Script:onInit()
    -- Item names/descriptions go through Locale; init is idempotent, so a
    -- scene without a Player (the FxScene tests) still gets its table.
    Locale.init()
    self.title = self:find("Title")
    self.memberLabel = self:find("MemberLabel")
    self.hint = self:find("Hint")
    self.statsText = self:find("Stats")
    self.detailName = self:find("DetailName")
    self.detailStats = self:find("DetailStats")
    self.detailDesc = self:find("DetailDesc")

    self.bagRows = {}
    for i = 1, BAG_ROWS do
        local button = self:find("BagRow" .. i)
        local label = self:find("BagRow" .. i .. "Label")
        if button and label then
            local index = i
            self.bagRows[#self.bagRows + 1] = { button = button, label = label }
            button.onClicked:add(self, function()
                self.bagIndex = index
                self:refresh()
            end)
        end
    end

    self.slotButtons = {}
    for slot, widgetName in pairs(SLOT_WIDGET) do
        local button = self:find(widgetName)
        local label = self:find(widgetName .. "Label")
        if button and label then
            local clickedSlot = slot
            self.slotButtons[slot] = { button = button, label = label }
            button.onClicked:add(self, function()
                self:doUnequip(clickedSlot)
            end)
        end
    end

    local function wire(widgetName, fn)
        local button = self:find(widgetName)
        if button == nil then
            return nil
        end
        button.onClicked:add(self, fn)
        return { button = button, label = self:find(widgetName .. "Label") }
    end
    self.equipButton = wire("EquipButton", function() self:doEquip() end)
    self.unequipButton = wire("UnequipButton", function() self:doUnequip(nil) end)
    local prev = self:find("PrevMember")
    if prev then
        prev.onClicked:add(self, function() self:cycleMember(-1) end)
    end
    local nextMember = self:find("NextMember")
    if nextMember then
        nextMember.onClicked:add(self, function() self:cycleMember(1) end)
    end

    self.bagIndex = 1
    self.memberIndex = 1
    self.rows = {}
    self.targetSlot = nil
    self.bOpen = false
    self.notice = nil
    self.openedFrame = -1
    self.root.visible = false
    self:applyLocale()
    -- A hidden widget is not ticked. The timer still runs, so the toggle
    -- key is seen while the panel is closed.
    self:every(0.001, function()
        if not input:isActionPressed("inventory") or dialogueBusy() or settingsBusy() then
            return
        end
        self:toggle()
    end)
    -- onUpdate only starts arriving once the script asks for it (same
    -- contract as Dialogue); without this the panel's keys are dead.
    self:setTickEnabled(true)
end

function Script:onUpdate(dt)
    if not self.bOpen then
        return
    end
    -- The frame that opened the panel is the same press; do not act on it.
    if time:getFrameIndex() == self.openedFrame then
        return
    end
    if input:isActionPressed("move_up") then
        self:moveSelection(-1)
    elseif input:isActionPressed("move_down") then
        self:moveSelection(1)
    elseif input:isActionPressed("move_left") then
        self:cycleMember(-1)
    elseif input:isActionPressed("move_right") then
        self:cycleMember(1)
    elseif input:isActionPressed("confirm") then
        self:doEquip()
    end
end

return Script
