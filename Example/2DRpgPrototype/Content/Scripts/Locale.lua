-- Localization sits on the engine's LocalizationManager (Core/Localization):
-- one table per language, keys are the English source strings, a missing key
-- translates to the key itself. The game's table ships as content data
-- (Content/Locale/zh.json) loaded through the engine `locale` module, the
-- same surface the editor uses, so a language switch repaints every panel
-- through one service.
--
-- Persist.locale stays the persisted choice ("en"/"zh"; missing = English).
local Locale = {}

function Locale.current()
    if Persist.locale == "zh" then
        return "zh"
    end
    return "en"
end

function Locale.tr(text)
    if text == nil then
        return ""
    end
    return locale.tr(text)
end

-- Load the game's tables once and apply the persisted choice. Safe to call
-- again (init re-runs on scene loads): loadLanguage replaces the table.
function Locale.init()
    local loaded = locale.languages()
    if loaded == nil or not loaded:find("zh", 1, true) then
        locale.loadTable("zh", "Content/Locale/zh.json")
    end
    Locale.set(Locale.current())
end

local function poke(id)
    local entry = ui.get(id)
    if type(entry) == "table" and type(entry.applyLocale) == "function" then
        entry:applyLocale()
    end
end

function Locale.refresh()
    poke("Dialogue")
    poke("Inventory")
    poke("Journal")
    poke("QuestNotice")
    poke("Settings")
    poke("BattleHUD")
end

function Locale.set(lang)
    Persist.locale = lang == "zh" and "zh" or "en"
    locale.setLanguage(Persist.locale == "zh" and "zh" or "")
    Locale.refresh()
end

function Locale.toggle()
    Locale.set(Locale.current() == "zh" and "en" or "zh")
end

return Locale
