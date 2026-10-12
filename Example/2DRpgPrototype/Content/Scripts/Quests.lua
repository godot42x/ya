-- Quest names live here. State lives in Persist.quests, so a save keeps
-- it and a scene transfer does not clear it. A value is "active" or "done".
local Locale = require("Locale")
local Quests = {}

local function notify(heading, title)
    local notice = ui.get("QuestNotice")
    if type(notice) ~= "table" or notice.show == nil then
        return
    end
    notice:show(heading, title)
end

Quests.catalog = {
    ruins = {
        title = "The overrun ruins",
        active = "Clear the monsters from the ruins east of camp.",
        done = "The ruins are quiet again.",
    },
    pot = {
        title = "The storage pot",
        active = "Look inside the storage pot in the house.",
        done = "You looked inside. A brass key was there.",
    },
    well = {
        title = "The quiet well",
        active = "Buy a well charm in the square, then hang it at the well.",
        done = "The well is awake again.",
    },
    cabinet = {
        title = "The locked cabinet",
        active = "A brass key opens the cabinet on the left side of the house.",
        done = "The cabinet gave up a shard of well glass.",
    },
}

function Quests.state(id)
    local quests = Persist.quests
    if type(quests) ~= "table" then
        return nil
    end
    return quests[id]
end

function Quests.start(id)
    local row = Quests.catalog[id]
    if row == nil then
        log:warn("Quests.start: unknown quest '" .. tostring(id) .. "'")
        return
    end
    local state = Quests.state(id)
    if state == "active" or state == "done" then
        return
    end
    Persist.quests = Persist.quests or {}
    Persist.quests[id] = "active"
    notify("Quest accepted", row.title)
end

function Quests.complete(id)
    local row = Quests.catalog[id]
    if row == nil then
        log:warn("Quests.complete: unknown quest '" .. tostring(id) .. "'")
        return
    end
    if Quests.state(id) == "done" then
        return
    end
    Persist.quests = Persist.quests or {}
    Persist.quests[id] = "done"
    notify("Quest complete", row.title)
end

function Quests.lines()
    local quests = Persist.quests
    if type(quests) ~= "table" then
        return {}
    end
    local lines = {}
    for id, state in pairs(quests) do
        local row = Quests.catalog[id]
        local title = Locale.tr(row and row.title or id)
        local detail
        if state == "done" then
            detail = Locale.tr(row and row.done or "done")
            lines[#lines + 1] = title .. "\n" .. Locale.tr("Done. ") .. detail
        else
            detail = Locale.tr(row and row.active or tostring(state))
            lines[#lines + 1] = title .. "\n" .. detail
        end
    end
    table.sort(lines)
    return lines
end

return Quests
