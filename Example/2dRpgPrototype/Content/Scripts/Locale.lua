-- English strings are the keys. Persist.locale is "en" or "zh"; missing
-- means English, which is what the play tests look for. Display sites call
-- tr(), and refresh() asks each open panel to paint again.
local Locale = {}

local zh = {
    ["Dialogue"] = "对话",
    ["Space / Enter / E"] = "空格 / 回车 / E",
    ["Up / Down, Space"] = "上 / 下，空格",
    ["Inventory"] = "物品",
    ["Nothing carried."] = "空空如也。",
    ["Journal"] = "任务",
    ["No quests."] = "没有任务。",
    ["Settings"] = "设置",
    ["Language"] = "语言",
    ["Quest accepted"] = "接受任务",
    ["Quest complete"] = "完成任务",
    ["Done. "] = "已完成。",
    ["Brass Key"] = "黄铜钥匙",
    ["Coin"] = "金币",
    ["Well Charm"] = "井之符",
    ["The storage pot"] = "储物罐",
    ["Look inside the storage pot in the house."] = "看看房子里的储物罐。",
    ["You looked inside. A brass key was there."] = "你看过了。里面有一把黄铜钥匙。",
    ["The quiet well"] = "安静的井",
    ["Buy a well charm in the square, then hang it at the well."] = "在广场买一枚井之符，再挂到井上。",
    ["The well is awake again."] = "井又醒了。",
    ["You opened the storage pot. Along with some coins, a brass key was inside."] = "你打开了储物罐。硬币旁边，有一把黄铜钥匙。",
    ["The pot is empty now."] = "罐子现在是空的。",
    ["The well is still. A charm from the square would wake it."] = "井还是静的。广场上的符能唤醒它。",
    ["The well answers. Tiny Town is awake again."] = "井回应了。小镇又醒了。",
    ["Here. Hang it at the well, north of the square."] = "拿去。挂到广场北边的井上。",
    ["You already carry the charm."] = "你已经带着这枚符了。",
    ["Three coins. Look in the storage pot."] = "要三枚硬币。去储物罐里找。",
    ["The well has gone quiet. The charm is three coins."] = "井安静下来了。这枚符要三枚硬币。",
    ["I'll buy the charm."] = "我买这枚符。",
    ["Not today."] = "今天就算了。",
    ["You already looked in the pot."] = "你已经看过罐子了。",
    ["A storage pot in the house. Have a look inside."] = "房子里有个储物罐。进去看看。",
    ["Welcome to Tiny Town!"] = "欢迎来到小镇！",
    ["Mind the fences -- the map keeps the roads, not me."] = "当心篱笆——路是地图记的，不是我。",
    ["Where is the well?"] = "井在哪里？",
    ["What's in the house?"] = "房子里有什么？",
    ["Goodbye"] = "再见",
    ["The well is north of the square."] = "井在广场北边。",
    ["Tiny Town -- drawn with the tile brush, kept by the tilemap."] = "小镇——用笔刷画出来，由地图记着。",
    ["Everything that stands still and has a name can be talked to."] = "站着不动、又有名字的，都能交谈。",
    ["The locked cabinet"] = "上锁的柜子",
    ["A brass key opens the cabinet on the left side of the house."] = "黄铜钥匙能打开房子左侧的柜子。",
    ["The cabinet gave up a shard of well glass."] = "柜子里有一块井之玻璃。",
    ["Well Glass"] = "井之玻璃",
    ["The well is still. A charm from the square, or glass from the house cabinet, would wake it."] = "井还是静的。广场上的符，或是房子柜子里的玻璃，都能唤醒它。",
    ["The glass hums, and the well answers. Tiny Town is awake again."] = "玻璃嗡鸣，井回应了。小镇又醒了。",
    ["What is the key for?"] = "这把钥匙是做什么的？",
    ["That key fits the cabinet on the left side of the house, not my till. The glass in there wakes the well too."] = "那把钥匙开的是房子左侧的柜子，不是我的钱箱。里面的玻璃也能唤醒井。",
    ["Two ways to wake the well: a charm from the stall, or glass from the locked cabinet."] = "唤醒井有两条路：摊上的符，或是上锁柜子里的玻璃。",
    ["The cabinet is locked. The brass key from the pot fits this lock."] = "柜子锁着。罐子里的黄铜钥匙能开这把锁。",
    ["The lock turns. A shard of well glass was waiting inside."] = "锁开了。里面放着一块井之玻璃。",
    ["The cabinet is empty now."] = "柜子现在是空的。",
}

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
    if Locale.current() ~= "zh" then
        return text
    end
    return zh[text] or text
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
end

function Locale.set(lang)
    Persist.locale = lang == "zh" and "zh" or "en"
    Locale.refresh()
end

function Locale.toggle()
    Locale.set(Locale.current() == "zh" and "en" or "zh")
end

return Locale
