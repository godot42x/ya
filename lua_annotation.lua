---@diagnostic disable: lowercase-global

---@param ... string
function add_packages(...) end

---@param ... string| "module.debug" | "module.release"
function add_rules(...) end

---@param ... string
function add_files(...) end

---@param ... string
function add_requires(...) end

---@class PackageConfig : table
---@field config table

---@overload fun(package:string, config:PackageConfig)
function add_requires(package, config) end

---@param type "binary"|"shared"|"static"
function set_kind(type) end

---@param lang "c++20"|"c++17"|"c++14"|"c++11"|"c++0x"|"c++98"
function set_languages(lang) end

---@param dir string
function set_targetdir(dir) end

---@param app string
function target(app) end
