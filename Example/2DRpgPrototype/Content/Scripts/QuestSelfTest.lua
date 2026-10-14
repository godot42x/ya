-- QuestSelfTest.lua -- G4: exercise the quest contract in a running game and
-- log one line per check, so an automation run can assert them from the log
-- (the PartySelfTest pattern). It runs once, then disables itself.
--
-- It pins the rules the whole quest system rests on, none of which need a
-- battle to observe:
--   * add() creates an active quest with its objectives at zero;
--   * add() is idempotent -- a second accept does not reset progress;
--   * advance() moves a counted objective and clamps it at its target;
--   * a quest completes itself when every objective is done;
--   * the reward is granted exactly once -- a second complete() pays nothing;
--   * the reward lands where the plan says: `exp` on the roster, `coin`/`items`
--     in the bag (Persist.items);
--   * getActive()/getCompleted() partition the log.
--
-- Battle-driven progress is covered end to end by quest_advance.py; this pins
-- the model the battle calls into.
local ScriptBase = require("ScriptBase")
local Quests = require("Quests")
local Party = require("Party")
local Items = require("Items")
local Script = ScriptBase:new()

local QUEST = "selftest_ruins"
local MEMBER = "questtest"

local function report(tag, ok, detail)
    log:info(string.format("QuestSelfTest %s %s %s", tag, ok and "OK" or "FAIL", detail or ""))
end

function Script:onInit()
    self.done = false
end

function Script:onUpdate(dt)
    if self.done then
        return
    end
    self.done = true

    -- A clean log, so a run does not inherit another test's quests.
    Persist.quests = {}
    Persist._questSeq = 0

    Quests.define(QUEST, {
        title = "Self-test ruins",
        active = "Advance every objective.",
        done = "All objectives are done.",
        objectives = {
            visit = { text = "Visit the place", trigger = "visited", scene = "FxScene" },
            kill = { text = "Defeat them", trigger = "battle_victory", target = 2 },
        },
        rewards = { exp = 20, coin = 7 },
    })

    -- A member for the exp reward to land on (its own id, so another test's
    -- roster entry cannot perturb the check).
    Party.ensure(MEMBER, "knight", "Quest Tester")

    -- add() creates an active quest whose objectives start at zero.
    Quests.add(QUEST)
    local q = Quests.get(QUEST)
    report("add_creates_active", q ~= nil and q.state == "active", tostring(q and q.state))
    report("objectives_start_zero",
           q.objectives.visit.done == false and q.objectives.kill.count == 0, "")

    -- add() is idempotent: a second accept does not reset progress.
    Quests.advance(QUEST, "kill", 1)
    Quests.add(QUEST)
    report("add_is_idempotent", Quests.get(QUEST).objectives.kill.count == 1,
           tostring(Quests.get(QUEST).objectives.kill.count))

    -- A battle victory advances the counted objective (through its trigger),
    -- and finishes the objective at its target of 2.
    Quests.notify("battle_victory", 1, { scene = "BattleRuins1" })
    report("victory_advances_to_target",
           Quests.get(QUEST).objectives.kill.count == 2
           and Quests.get(QUEST).objectives.kill.done, tostring(Quests.get(QUEST).objectives.kill.count))
    report("not_complete_until_all_done", not Quests.isCompleted(QUEST), "")

    -- Capture the member before the completing step, so the exp check is a
    -- real before/after (a completed quest pays its reward exactly here).
    local before = Party.roster()[MEMBER]
    local beforeLevel, beforeExp = before.level, before.exp
    local coinBefore = Items.count("coin")

    -- A scene visit advances its objective; that completes the quest and pays.
    Quests.notify("visited", 1, { scene = "FxScene" })
    report("visit_advances", Quests.get(QUEST).objectives.visit.done == true, "")
    report("completes_when_all_done", Quests.isCompleted(QUEST), tostring(Quests.state(QUEST)))

    -- The reward went where the plan says: coin in the bag, exp on the roster
    -- (20 exp is exactly one level from level 1).
    report("reward_coin_in_bag", Items.count("coin") == coinBefore + 7,
           string.format("%d -> %d", coinBefore, Items.count("coin")))
    report("reward_exp_on_roster", before.level == beforeLevel + 1 and before.exp == beforeExp,
           string.format("Lv %d -> %d", beforeLevel, before.level))

    -- The reward is paid exactly once.
    local paid = Quests.complete(QUEST)
    report("second_complete_pays_nothing", paid == nil, tostring(paid))
    report("coin_unchanged_after_second", Items.count("coin") == coinBefore + 7,
           tostring(Items.count("coin")))
    report("level_unchanged_after_second", before.level == beforeLevel + 1,
           tostring(before.level))

    -- getActive()/getCompleted() partition the log.
    local activeIds, doneIds = {}, {}
    for _, e in ipairs(Quests.getActive()) do activeIds[#activeIds + 1] = e.id end
    for _, e in ipairs(Quests.getCompleted()) do doneIds[#doneIds + 1] = e.id end
    report("completed_lists_quest", #doneIds == 1 and doneIds[1] == QUEST, table.concat(doneIds, ","))
    report("active_excludes_completed", #activeIds == 0, table.concat(activeIds, ","))

    report("ready", true, "")
end

return Script
