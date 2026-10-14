-- Quests.lua -- the quest system. A quest's *definition* (title, description,
-- objectives, rewards) lives in `Quests.catalog`; its *state* lives in
-- `Persist.quests`, so a save keeps it and a scene transfer does not clear it.
--
-- State model (Persist.quests[id]):
--   { state = "active" | "completed",
--     objectives = { [key] = { done = bool, count = n, target = n } },
--     rewarded = bool,
--     seq = n }
--
-- An objective's target, display text and *trigger* come from the definition.
-- Nothing polls: a system that observes progress calls
--   Quests.advance(id, key, amount)      -- one objective
--   Quests.notify(trigger, amount, ctx)  -- every active objective that declares
--                                           that trigger (battle won, scene
--                                           entered, ...)
-- and the quest completes itself -- and pays its reward -- once every objective
-- is done. Reward settlement is exactly-once: `complete` marks the quest paid
-- before granting, and a second call for the same quest is a no-op.
--
-- Rewards settle at completion: `exp` goes to every party roster member,
-- `coin`/`items` into the bag (`Persist.items`).
local Locale = require("Locale")
local Quests = {}

Quests.catalog = {
    tutorial_ruins = {
        title = "The overrun ruins",
        active = "Clear the monsters from the ruins east of camp.",
        done = "The ruins are quiet again.",
        objectives = {
            visit_ruins = {
                text = "Reach the ruins",
                trigger = "visited",
                scene = "BattleRuins1",
            },
            defeat_enemies = {
                text = "Defeat the monsters in the ruins",
                trigger = "battle_victory",
                target = 3,
            },
        },
        rewards = { exp = 40, coin = 15 },
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

local function notify(heading, title)
    local notice = ui.get("QuestNotice")
    if type(notice) ~= "table" or notice.show == nil then
        return
    end
    notice:show(heading, title)
end

local function table_()
    Persist.quests = Persist.quests or {}
    return Persist.quests
end

local function nextSeq()
    Persist._questSeq = (Persist._questSeq or 0) + 1
    return Persist._questSeq
end

-- The definition of one quest, or nil.
function Quests.def(id)
    return Quests.catalog[id]
end

-- Register (or merge) a quest definition. Content may pass a partial def; it is
-- folded over the catalog entry, so text ships with the content and state with
-- this module.
function Quests.define(id, def)
    local base = Quests.catalog[id] or {}
    if type(def) == "table" then
        for k, v in pairs(def) do
            base[k] = v
        end
    end
    Quests.catalog[id] = base
    return base
end

-- The state entry for one quest, or nil.
function Quests.get(id)
    local quests = Persist.quests
    if type(quests) ~= "table" then
        return nil
    end
    return quests[id]
end

-- The state as a string ("active" / "completed"), legacy "done" accepted.
function Quests.state(id)
    local q = Quests.get(id)
    if type(q) == "string" then
        return q == "done" and "completed" or q
    end
    return type(q) == "table" and q.state or nil
end

function Quests.isActive(id)
    return Quests.state(id) == "active"
end

function Quests.isCompleted(id)
    local state = Quests.state(id)
    return state == "completed" or state == "done"
end

-- Build the runtime objectives for a def: every declared objective starts at
-- count 0 / not done, with its target copied so advancing never touches the
-- catalog.
local function buildObjectives(def)
    local out = {}
    for key, o in pairs(def.objectives or {}) do
        out[key] = { done = false, count = 0, target = o.target or 1 }
    end
    return out
end

-- Add (accept) a quest. Idempotent: an existing quest is left untouched and
-- returned, so a talk page that re-shows does not re-notify.
function Quests.add(id, def)
    local existing = Quests.get(id)
    if type(existing) == "table" then
        return existing
    end
    local d = Quests.define(id, def)
    local quest = {
        state = "active",
        objectives = buildObjectives(d),
        rewarded = false,
        seq = nextSeq(),
    }
    table_()[id] = quest
    notify("Quest accepted", d.title or id)
    log:info("Quests: accepted '" .. tostring(id) .. "'")
    return quest
end

-- v1 alias: start == add.
function Quests.start(id)
    return Quests.add(id)
end

-- The quest's definition merged with its live objective progress, for a caller
-- that wants text and state together (the journal).
function Quests.describe(id)
    local q = Quests.get(id)
    if type(q) ~= "table" then
        return nil
    end
    local d = Quests.def(id) or {}
    local objectives = {}
    for key, o in pairs(q.objectives or {}) do
        local od = (d.objectives or {})[key] or {}
        objectives[#objectives + 1] = {
            key = key,
            text = od.text or key,
            done = o.done,
            count = o.count,
            target = o.target or 1,
        }
    end
    table.sort(objectives, function(a, b)
        return a.key < b.key
    end)
    return {
        id = id,
        title = d.title or id,
        active = d.active or "",
        done = d.done or "",
        state = q.state,
        objectives = objectives,
        rewards = d.rewards,
        rewarded = q.rewarded,
    }
end

-- Grant a quest's rewards. `exp` goes to every roster member, `coin` and
-- `items` into the bag. Returns the granted table.
local function grantRewards(id, rewards)
    if type(rewards) ~= "table" then
        return {}
    end
    if rewards.exp and rewards.exp > 0 then
        local Party = require("Party")
        for memberId in pairs(Party.roster()) do
            Party.gainExp(memberId, rewards.exp)
        end
    end
    local Items = require("Items")
    if rewards.coin and rewards.coin > 0 then
        Items.give("coin", rewards.coin)
    end
    for item, count in pairs(rewards.items or {}) do
        Items.give(item, count)
    end
    log:info(string.format("Quests: reward '%s' exp=%d coin=%d", tostring(id),
        rewards.exp or 0, rewards.coin or 0))
    return rewards
end

-- Complete a quest and settle its reward exactly once. `rewards` overrides the
-- def (the linear town errands complete on an action rather than a count).
-- A def without live state (never accepted through `add`) is created on the
-- spot, so an action that ends a quest does not have to be preceded by a talk.
-- Returns the granted table, or nil when unknown or already paid.
function Quests.complete(id, rewards)
    local q = Quests.get(id)
    if type(q) ~= "table" then
        if Quests.def(id) == nil then
            log:warn("Quests.complete: unknown quest '" .. tostring(id) .. "'")
            return nil
        end
        q = { state = "active", objectives = {}, rewarded = false, seq = nextSeq() }
        table_()[id] = q
    end
    if q.rewarded then
        return nil
    end
    local d = Quests.def(id) or {}
    q.state = "completed"
    q.rewarded = true
    local paid = grantRewards(id, rewards or d.rewards)
    notify("Quest complete", d.title or id)
    log:info("Quests: completed '" .. tostring(id) .. "'")
    return paid
end

-- Advance one objective by `amount` (default 1), clamped to its target. When
-- every objective is done the quest completes. Returns true when something
-- changed.
function Quests.advance(id, key, amount)
    local q = Quests.get(id)
    if type(q) ~= "table" or q.state ~= "active" then
        return false
    end
    local o = (q.objectives or {})[key]
    if type(o) ~= "table" or o.done then
        return false
    end
    local target = o.target or 1
    local count = math.min(target, (o.count or 0) + (amount or 1))
    if count <= (o.count or 0) then
        return false
    end
    o.count = count
    if count >= target then
        o.done = true
        log:info(string.format("Quests: objective '%s.%s' done", tostring(id), tostring(key)))
    end
    Quests.tryComplete(id)
    return true
end

-- Complete the quest if every objective is done and it is still active. A
-- quest with no objectives waits for an explicit `complete`.
function Quests.tryComplete(id)
    local q = Quests.get(id)
    if type(q) ~= "table" or q.state ~= "active" then
        return false
    end
    local any = false
    for _, o in pairs(q.objectives or {}) do
        any = true
        if not o.done then
            return false
        end
    end
    if not any then
        return false
    end
    Quests.complete(id)
    return true
end

local function matchesScene(def, ctx)
    return def.scene == nil or def.scene == "" or (ctx ~= nil and def.scene == ctx.scene)
end

-- Advance every active objective that declares this trigger (and whose scene
-- filter matches `ctx.scene`, when it has one). Called by the systems that
-- observe progress; a quest completes itself when its objectives are done.
function Quests.notify(trigger, amount, ctx)
    local quests = Persist.quests
    if type(quests) ~= "table" then
        return
    end
    local ids = {}
    for id, q in pairs(quests) do
        if type(q) == "table" and q.state == "active" then
            ids[#ids + 1] = id
        end
    end
    for _, id in ipairs(ids) do
        local def = Quests.def(id) or {}
        for key, od in pairs(def.objectives or {}) do
            if od.trigger == trigger and matchesScene(od, ctx) then
                Quests.advance(id, key, amount)
            end
        end
    end
end

-- Every quest of a state, ordered by the order they were accepted.
local function collect(state)
    local quests = Persist.quests
    if type(quests) ~= "table" then
        return {}
    end
    local rows = {}
    for id, q in pairs(quests) do
        if type(q) == "table" and q.state == state then
            rows[#rows + 1] = { id = id, seq = q.seq or 0 }
        end
    end
    table.sort(rows, function(a, b)
        if a.seq ~= b.seq then
            return a.seq < b.seq
        end
        return a.id < b.id
    end)
    local out = {}
    for i, row in ipairs(rows) do
        out[i] = Quests.describe(row.id)
    end
    return out
end

function Quests.getActive()
    return collect("active")
end

function Quests.getCompleted()
    return collect("completed")
end

-- "Reach the ruins [x]  Defeat the monsters in the ruins 2/3" -- one line per
-- objective. A counted objective shows count/target, the rest a checkbox.
function Quests.objectiveLine(objective)
    local marker = objective.done and "[x]" or "[ ]"
    local text = Locale.tr(objective.text)
    if (objective.target or 1) > 1 then
        text = string.format("%s  %d/%d", text, objective.count or 0, objective.target)
    end
    return string.format("%s %s", marker, text)
end

-- "Reward: 40 exp, 15 coin", or "" when the quest pays nothing.
function Quests.rewardLine(entry)
    local r = entry.rewards
    if type(r) ~= "table" then
        return ""
    end
    local parts = {}
    if r.exp and r.exp > 0 then
        parts[#parts + 1] = string.format("%d exp", r.exp)
    end
    if r.coin and r.coin > 0 then
        parts[#parts + 1] = string.format("%d coin", r.coin)
    end
    if #parts == 0 then
        return ""
    end
    return Locale.tr("Reward") .. ": " .. table.concat(parts, ", ")
end

return Quests
