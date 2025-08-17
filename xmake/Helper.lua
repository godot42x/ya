---@class ModuleRule
---@field type "static"|"shared"|"binary"

---@param name string
---@param rule ModuleRule
function YaModule(name, rule)
    target(name)
    do
        set_kind(rule.type)
        add_files("Source/**.cpp")
    end
end

_G["YaModule"] = YaModule
