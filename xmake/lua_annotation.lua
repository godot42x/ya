---@diagnostic disable: lowercase-global, unused-local, unused-vararg
--
-- xmake API annotations for the Lua language server (LuaLS / EmmyLua-LS).
--
-- xmake resolves the build APIs below as globals: `add_files`, `set_kind`, `on_config`, ...
-- are looked up in the scope that is currently open (`target` / `option` / `rule` /
-- `task` / `package` / `toolchain`) and fall back to the root (project) scope, so a flat
-- list of globals is what the editor needs.
--
-- Parameters are typed loosely on purpose: xmake accepts literals, option tables and custom
-- values, so a strict union such as `"windows"|"linux"` would report working build scripts
-- as errors. The accepted values are named in the description of each API instead.
--
-- The surface mirrors xmake v3.0.8 `interpreter:api_define()`:
--   core/project/{project,target,option,rule,package}.lua
--   core/language/*/load.lua, core/base/task.lua, core/tool/toolchain.lua
--   core/base/interpreter.lua and core/sandbox/modules/ (builtins)
-- Project-local helpers (`YaModule`, `ya_std_module`, `ya_test_sources`,
-- `ya_engine_defines`, `include_xmake`, ...) are defined by the scripts that own them and
-- are not repeated here.
--
-- The stub bodies below return placeholder values and are never executed: this file is
-- not included by any build, it only feeds the language server.

-- --------------------------------------------------------------------------
-- script bootstrap
-- --------------------------------------------------------------------------

---Include an xmake script: a directory, an `xmake.lua`, or an explicit `*.lua` file.
---@param ... string
function includes(...) end

---Include scripts from sub-directories (`add_subdirs("src/*")`).
---@param ... string
function add_subdirs(...) end

---@param ... string
function add_subfiles(...) end

---Require a minimum xmake version, e.g. `set_xmakever("3.0.0")`.
---@param version string
function set_xmakever(version) end

---Open a namespace: everything declared inside it is prefixed with `name::`.
---@param name string
---@param callback fun(...)
function namespace(name, callback) end

---Close the namespace opened by `namespace()`.
function namespace_end() end

---Import an xmake module. The bare form binds the module to a global named after it,
---e.g. `import("lib.detect.find_tool")` defines the global `find_tool`.
---@param module string  e.g. "core.project.depend", "utils.progress", "lib.detect.find_tool"
---@param opt? table     e.g. {alias = "name", anonymous = true, rootdir = "..."}
---@return table
function import(module, opt) return {} end

---@param pattern string
---@param opt? table  e.g. {includedirs = ..., linkdirs = ..., configs = ...}
---@return table|nil
function find_package(pattern, opt) return nil end

---@param pattern string
---@param opt? table
---@return table
function find_packages(pattern, opt) return {} end

-- Print to stdout; the *printf variants take a format string, the other variants add
-- color (c), stderr (w), verbose (v) or debug (d) output.
---@param ... any
function print(...) end
---@param format string
---@param ... any
function printf(format, ...) end
---@param ... any
function cprint(...) end
---@param format string
---@param ... any
function cprintf(format, ...) end
---@param ... any
function dprint(...) end
---@param format string
---@param ... any
function dprintf(format, ...) end
---@param ... any
function vprint(...) end
---@param format string
---@param ... any
function vprintf(format, ...) end
---@param ... any
function wprint(...) end
---@param format string
---@param ... any
function format(format, ...) end
---@param format string
---@param ... any
function vformat(format, ...) end

---Abort the configuration with an error message.
---@param format string
---@param ... any
function raise(format, ...) end

---xmake exception handling:
---`try { function () ... end } catch { function (errors) ... end } finally { ... }`.
---@param ... function|table
function try(...) end
---@param ... function|table
function catch(...) end
---@param ... function|table
function finally(...) end

-- --------------------------------------------------------------------------
-- scope declarations
-- --------------------------------------------------------------------------

---Declare a target; the block after it configures that target.
---@param name string
---@param callback? fun(target: table)
function target(name, callback) end
---Close the target opened by `target()`.
function target_end() end

---Declare a build option (`xmake f --name=value`).
---@param name string
---@param callback? fun(option: table)
function option(name, callback) end
---Close the option opened by `option()`.
function option_end() end

---Declare a custom build rule.
---@param name string
---@param callback? fun(rule: table)
function rule(name, callback) end
---Close the rule opened by `rule()`.
function rule_end() end

---Declare a task (`xmake <name>`).
---@param name string
---@param callback? fun(task: table)
function task(name, callback) end
---Close the task opened by `task()`.
function task_end() end

---Declare a package, required with `add_requires("...")`.
---@param name string
---@param callback? fun(package: table)
function package(name, callback) end
---Close the package opened by `package()`.
function package_end() end

---Declare a toolchain.
---@param name string
---@param callback? fun(toolchain: table)
function toolchain(name, callback) end
---Close the toolchain opened by `toolchain()`.
function toolchain_end() end

-- --------------------------------------------------------------------------
-- project scope (xmake.lua root)
-- --------------------------------------------------------------------------

---@param name string
function set_project(name) end

---Version of the project, target or package.
---@param version string
function set_version(version) end

---Description of the project, option, target, toolchain or package.
---@param description string
function set_description(description) end

---Modes that may be configured, e.g. `set_allowedmodes("debug", "release")`.
---@param ... string
function set_allowedmodes(...) end
---@param ... string
function set_allowedplats(...) end
---@param ... string
function set_allowedarchs(...) end

---Default mode: "debug" | "releasedbg" | "release" | "profile".
---@param mode string
function set_defaultmode(mode) end
---Default platform: "macosx" | "linux" | "windows" | "mingw" | "bsd" | "android" | "ios" | "wasm" | "cross".
---@param plat string
function set_defaultplat(plat) end
---Default architecture: "x86_64" | "i386" | "arm64" | "armv7" | "riscv64".
---@param arch string
function set_defaultarchs(arch) end

---Set a project level configuration value.
---@param name string
---@param value string|number|boolean|table
function set_config(name, value) end

---Add remote package repositories:
---`add_repositories("my-repo https://github.com/me/repo.git")`.
---@param repos string|table
---@param opt? table  e.g. {rootdir = "..."}
function add_repositories(repos, opt) end

-- Directory based lookups for modules, plugins, platforms, toolchains and packages.
---@param ... string
function add_moduledirs(...) end
---@param ... string
function add_plugindirs(...) end
---@param ... string
function add_platformdirs(...) end
---@param ... string
function add_toolchaindirs(...) end
---Directories that hold package definitions.
---@param ... string
function add_packagedirs(...) end

---Add required packages; the trailing option table takes `configs`, `public`, `optional`,
---`system`, `alias`, `verify`, `host`, `installonly`, ...
---@param ... string|table
function add_requires(...) end

---Override the configuration of required packages, e.g.
---`add_requireconfs("freetype", {configs = {shared = false}})`.
---@param ... string|table
function add_requireconfs(...) end

-- --------------------------------------------------------------------------
-- configuration queries
-- --------------------------------------------------------------------------

---Is the current platform one of the given ones? e.g. `is_plat("macosx")`.
---@param ... string
---@return boolean
function is_plat(...) return false end
---@param ... string
---@return boolean
function is_arch(...) return false end
---Is the current build mode one of the given ones? e.g. `is_mode("debug")`.
---@param ... string
---@return boolean
function is_mode(...) return false end
---@param ... string
---@return boolean
function is_kind(...) return false end
---@param ... string
---@return boolean
function is_os(...) return false end
---Is the build host the given system? e.g. `is_host("macosx")`.
---@param ... string
---@return boolean
function is_host(...) return false end
---@param ... string
---@return boolean
function is_subhost(...) return false end

---Is the current platform/architecture a cross compilation target?
---@return boolean
function is_cross() return false end

---Compare a configuration value, e.g. `is_config("kind", "static")`.
---@param name string
---@param ... string|number|boolean
---@return boolean
function is_config(name, ...) return false end

---Get a configuration value, e.g. `get_config("ya_profile")`.
---@param name string
---@return any
function get_config(name) return nil end

---@param ... string
---@return boolean
function has_config(...) return false end
---@param ... string
---@return boolean
function has_package(...) return false end

-- --------------------------------------------------------------------------
-- target: kind, meta and toolchain
-- --------------------------------------------------------------------------

---"binary" | "static" | "shared" | "object" | "headeronly" | "moduleonly" | "phony",
---or a custom kind registered by a rule.
---@param kind string
function set_kind(kind) end

---@param plat string
function set_plat(plat) end
---@param arch string
function set_arch(arch) end
---@param license string
function set_license(license) end

---Group used by `xmake f -g <group>` and `xmake build <group>`.
---@param group string
function set_group(group) end

---@param filename string
function set_filename(filename) end
---@param basename string
function set_basename(basename) end
---@param extension string
function set_extension(extension) end
---@param prefixname string
function set_prefixname(prefixname) end
---@param suffixname string
function set_suffixname(suffixname) end
---@param dir string
function set_prefixdir(dir) end

---Enable or disable the target in the build graph, and its menu entry.
---@param enabled boolean
function set_enabled(enabled) end
---Default value of the option, or whether the target is built by default.
---@param default any
function set_default(default) end

---Debug symbols: "none" | "debug" | "all".
---@param symbols string
function set_symbols(symbols) end
---Strip: "none" | "debug" | "all".
---@param strip string
function set_strip(strip) end
---Optimization: "none" | "fast" | "faster" | "fastest" | "smallest" | "debug" | "aggressive".
---@param level string
function set_optimize(level) end
---Warnings: "none" | "less" | "more" | "all" | "extra" | "everything" | "error".
---@param ... string
function set_warnings(...) end
---Exceptions: "cxx" | "objc" | "no-cxx" | "no-objc" | "none".
---@param ... string
function set_exceptions(...) end
---MSVC runtime: "MT" | "MTd" | "MD" | "MDd" | "none".
---@param ... string
function set_runtimes(...) end
---Language standard: `set_languages("c++20")`, `set_languages("c11", "gnu++20")`.
---@param ... string
function set_languages(...) end
---@param ... string
function add_languages(...) end
---Vector extensions: "sse" | "sse2" | "avx" | "avx2" | "neon".
---@param ... string
function add_vectorexts(...) end
---Source encoding, e.g. `set_encodings("utf-8")`.
---@param encoding string
function set_encodings(encoding) end
---Float model: "fast" | "strict" | "precise".
---@param model string
function set_fpmodels(model) end

---@param ... string|table
function set_toolchains(...) end
---@param ... string|table
function add_toolchains(...) end
---@param name string|table
function set_toolset(name) end

---Override a policy, e.g. `set_policy("build.warning", true)`.
---@param name string
---@param value boolean|string|number
function set_policy(name, value) end

---@param name string
---@param value string|number|boolean
function set_configvar(name, value) end

---@param ... any
function set_values(...) end
---@param ... any
function add_values(...) end

---Environment variables and extra arguments used by `xmake run`.
---@param ... any
function set_runenv(...) end
---@param ... any
function add_runenvs(...) end
---@param ... string
function set_runargs(...) end

---Rules, options and imports attached to the target, e.g.
---`add_rules("c++.unity_build", {batchsize = 6})`.
---@param ... string|table
function add_rules(...) end
---@param ... string|table
function set_rules(...) end
---@param ... string|table
function add_options(...) end
---@param ... string|table
function set_options(...) end
---@param ... string|table
function add_imports(...) end
---@param ... string|table
function add_tests(...) end
---@param ... string|table
function add_filegroups(...) end

-- --------------------------------------------------------------------------
-- target: directories
-- --------------------------------------------------------------------------

---@param dir string
function set_targetdir(dir) end
---@param dir string
function set_objectdir(dir) end
---@param dir string
function set_dependir(dir) end
---@param dir string
function set_autogendir(dir) end
---@param dir string
function set_configdir(dir) end
---@param dir string
function set_installdir(dir) end
---Directory `xmake run` executes in.
---@param dir string
function set_rundir(dir) end

---Precompiled headers for c / c++ / objc / objc++ sources.
---@param headerfile string
function set_pcheader(headerfile) end
---@param headerfile string
function set_pcxxheader(headerfile) end
---@param headerfile string
function set_pmheader(headerfile) end
---@param headerfile string
function set_pmxxheader(headerfile) end

-- --------------------------------------------------------------------------
-- target: sources, headers and directories
-- --------------------------------------------------------------------------

---Add source files, e.g. `add_files("Source/**.cpp")`. The trailing option table takes
---`rules`, `sourcekind`, `unity_ignored`, `always_added`, `group`, `values`, ...
---@param ... string|table
function add_files(...) end

---@param ... string|table
function add_headerfiles(...) end
---@param ... string|table
function add_configfiles(...) end
---@param ... string|table
function add_installfiles(...) end
---@param ... string|table
function add_extrafiles(...) end
---@param ... string|table
function add_cleanfiles(...) end

---@param ... string|table
function remove_files(...) end
---@param ... string|table
function remove_headerfiles(...) end
---@param ... string|table
function remove_configfiles(...) end
---@param ... string|table
function remove_installfiles(...) end
---@param ... string|table
function remove_extrafiles(...) end
---@param ... string|table
function del_files(...) end

---Add include directories; `{public = true}` exports them to dependent targets.
---@param ... string|table
function add_includedirs(...) end
---@param ... string|table
function add_sysincludedirs(...) end
---@param ... string|table
function add_linkdirs(...) end
---@param ... string|table
function add_frameworkdirs(...) end
---@param ... string|table
function add_rpathdirs(...) end
---@param ... string|table
function add_embeddirs(...) end

-- --------------------------------------------------------------------------
-- target: links, defines and flags
-- --------------------------------------------------------------------------

---@param ... string|table
function add_links(...) end
---@param ... string|table
function add_syslinks(...) end
---Order/group libraries on the link line (needed for archives that must be repeated).
---@param ... string|table
function add_linkorders(...) end
---@param ... string|table
function add_linkgroups(...) end
---macOS/iOS frameworks.
---@param ... string|table
function add_frameworks(...) end

---Add preprocessor definitions; `{public = true}` exports them to dependent targets.
---@param ... string|table
function add_defines(...) end
---@param ... string|table
function add_undefines(...) end
---@param ... string|table
function add_forceincludes(...) end

---Compiler flags; `{force = true}` skips the flag support check.
---@param ... string|table
function add_cxflags(...) end
---@param ... string|table
function add_cflags(...) end
---@param ... string|table
function add_cxxflags(...) end
---@param ... string|table
function add_mflags(...) end
---@param ... string|table
function add_mxflags(...) end
---@param ... string|table
function add_mxxflags(...) end
---@param ... string|table
function add_asflags(...) end
---@param ... string|table
function add_ldflags(...) end
---@param ... string|table
function add_shflags(...) end
---@param ... string|table
function add_arflags(...) end

-- Languages that are not built here; same shape as the C/C++ flag APIs above:
-- csharp `add_csflags`, cuda `add_cuflags`/`add_culdflags`/`add_cugencodes`,
-- dlang `add_dcflags`, fortran `add_fcflags`, golang `add_gcflags`, kotlin `add_kcflags`,
-- msrc `add_mrcflags`, nim `add_ncflags`, pascal `add_pcflags`, rust `add_rcflags`,
-- swift `add_scflags`, zig `add_zcflags`.
---@param ... string|table
function add_csflags(...) end
---@param ... string|table
function add_cuflags(...) end
---@param ... string|table
function add_culdflags(...) end
---@param ... string|table
function add_cugencodes(...) end
---@param ... string|table
function add_dcflags(...) end
---@param ... string|table
function add_fcflags(...) end
---@param ... string|table
function add_gcflags(...) end
---@param ... string|table
function add_kcflags(...) end
---@param ... string|table
function add_mrcflags(...) end
---@param ... string|table
function add_ncflags(...) end
---@param ... string|table
function add_pcflags(...) end
---@param ... string|table
function add_rcflags(...) end
---@param ... string|table
function add_scflags(...) end
---@param ... string|table
function add_zcflags(...) end

-- --------------------------------------------------------------------------
-- target: dependencies and packages
-- --------------------------------------------------------------------------

---Add target dependencies; `{public = true}` re-exports the dependency to dependents.
---@param ... string|table
function add_deps(...) end

---Add packages; the trailing option table takes `configs`, `public`, `optional`, ...
---@param ... string|table
function add_packages(...) end

-- --------------------------------------------------------------------------
-- build phase hooks (target, rule, option, package and task scopes)
-- --------------------------------------------------------------------------

-- `on_<phase>(callback)` runs the callback during that phase; `before_<phase>` and
-- `after_<phase>` run it before/after the phase. A hook takes optional platform/arch
-- patterns in front of the callback and an optional trailing config table, e.g.
-- `on_install("macosx", "linux", function (package) ... end)`, which is why the parameters
-- below are permissive. The callback receives the current target/rule/package/option/task
-- instance plus phase specific arguments, e.g.
-- `before_buildcmd_file(function (target, batchcmds, sourcefile, opt) ... end)`.

---@param ... any
function on_load(...) end
---@param ... any
function on_config(...) end
---@param ... any
function on_prepare(...) end
---@param ... any
function on_prepare_file(...) end
---@param ... any
function on_prepare_files(...) end
---@param ... any
function on_build(...) end
---@param ... any
function on_build_file(...) end
---@param ... any
function on_build_files(...) end
---@param ... any
function on_link(...) end
---@param ... any
function on_clean(...) end
---@param ... any
function on_package(...) end
---@param ... any
function on_install(...) end
---@param ... any
function on_uninstall(...) end
---@param ... any
function on_run(...) end
---@param ... any
function on_test(...) end
---@param ... any
function on_preparecmd(...) end
---@param ... any
function on_preparecmd_file(...) end
---@param ... any
function on_preparecmd_files(...) end
---@param ... any
function on_buildcmd(...) end
---@param ... any
function on_buildcmd_file(...) end
---@param ... any
function on_buildcmd_files(...) end
---@param ... any
function on_linkcmd(...) end
---@param ... any
function on_installcmd(...) end
---@param ... any
function on_uninstallcmd(...) end

---@param ... any
function before_load(...) end
---@param ... any
function before_config(...) end
---@param ... any
function before_prepare(...) end
---@param ... any
function before_prepare_file(...) end
---@param ... any
function before_prepare_files(...) end
---@param ... any
function before_build(...) end
---@param ... any
function before_build_file(...) end
---@param ... any
function before_build_files(...) end
---@param ... any
function before_link(...) end
---@param ... any
function before_clean(...) end
---@param ... any
function before_package(...) end
---@param ... any
function before_install(...) end
---@param ... any
function before_uninstall(...) end
---@param ... any
function before_run(...) end
---@param ... any
function before_test(...) end
---@param ... any
function before_preparecmd(...) end
---@param ... any
function before_preparecmd_file(...) end
---@param ... any
function before_preparecmd_files(...) end
---@param ... any
function before_buildcmd(...) end
---@param ... any
function before_buildcmd_file(...) end
---@param ... any
function before_buildcmd_files(...) end
---@param ... any
function before_linkcmd(...) end
---@param ... any
function before_installcmd(...) end
---@param ... any
function before_uninstallcmd(...) end

---@param ... any
function after_load(...) end
---@param ... any
function after_config(...) end
---@param ... any
function after_prepare(...) end
---@param ... any
function after_prepare_file(...) end
---@param ... any
function after_prepare_files(...) end
---@param ... any
function after_build(...) end
---@param ... any
function after_build_file(...) end
---@param ... any
function after_build_files(...) end
---@param ... any
function after_link(...) end
---@param ... any
function after_clean(...) end
---@param ... any
function after_package(...) end
---@param ... any
function after_install(...) end
---@param ... any
function after_uninstall(...) end
---@param ... any
function after_run(...) end
---@param ... any
function after_test(...) end
---@param ... any
function after_preparecmd(...) end
---@param ... any
function after_preparecmd_file(...) end
---@param ... any
function after_preparecmd_files(...) end
---@param ... any
function after_buildcmd(...) end
---@param ... any
function after_buildcmd_file(...) end
---@param ... any
function after_buildcmd_files(...) end
---@param ... any
function after_linkcmd(...) end
---@param ... any
function after_installcmd(...) end
---@param ... any
function after_uninstallcmd(...) end

-- --------------------------------------------------------------------------
-- option scope
-- --------------------------------------------------------------------------

---Show the option in `xmake f --menu`.
---@param show boolean
function set_showmenu(show) end

---Category of the option or task in the menu.
---@param category string
function set_category(category) end

---Enable a feature, checked with `has_features()` style APIs.
---@param ... string|table
function add_features(...) end

-- Source checks of the option; they are also filled in by `has_cfuncs`, `has_cxxflags`, ...
---@param ... string|table
function add_cincludes(...) end
---@param ... string|table
function add_cfuncs(...) end
---@param ... string|table
function add_ctypes(...) end
---@param ... string|table
function add_csnippets(...) end
---@param ... string|table
function add_cxxincludes(...) end
---@param ... string|table
function add_cxxfuncs(...) end
---@param ... string|table
function add_cxxtypes(...) end
---@param ... string|table
function add_cxxsnippets(...) end

---@param ... any
function before_check(...) end
---@param ... any
function on_check(...) end
---@param ... any
function after_check(...) end

-- --------------------------------------------------------------------------
-- rule scope
-- --------------------------------------------------------------------------

---Source file extensions the rule claims, e.g. `set_extensions(".h", ".hpp")`.
---@param ... string
function set_extensions(...) end
---@param ... string
function set_sourcekinds(...) end
---Order the rule after the given rules when both apply to the same file.
---@param ... string
function add_orders(...) end

-- --------------------------------------------------------------------------
-- task scope
-- --------------------------------------------------------------------------

---`set_menu({usage = "xmake <name>", description = "..."})`.
---@param menu table|function
function set_menu(menu) end

-- --------------------------------------------------------------------------
-- package scope
-- --------------------------------------------------------------------------

---@param ... string|table
function add_urls(...) end
---@param urls string|table
---@param opt? table
function set_urls(urls, opt) end
---Versions of the package and their hashes, e.g. `add_versions("1.0", "sha256")`.
---@param ... string|table
function add_versions(...) end
---@param ... string|table
function add_versionfiles(...) end
---@param ... string|table
function add_patches(...) end
---@param ... string|table
function add_resources(...) end

---Base package the current package extends.
---@param name string
function set_base(name) end
---@param tips string
function set_installtips(tips) end
---@param homepage string
function set_homepage(homepage) end
---@param parallelize boolean|number
function set_parallelize(parallelize) end
---@param dir string
function set_sourcedir(dir) end
---@param dir string
function set_cachedir(dir) end

---@param ... string|table
function add_bindirs(...) end
---@param ... string|table
function add_schemes(...) end
---@param ... string|table
function add_extsources(...) end
---@param ... string|table
function add_components(...) end
---@param ... any
function on_component(...) end

---Declare a package configuration, e.g.
---`add_configs("shared", {description = "...", default = false, type = "boolean"})`.
---@param name string
---@param config? table  e.g. {description = string, default = any, type = string,
---                            values = table, readonly = boolean}
function add_configs(name, config) end

---@param ... any
function on_source(...) end
---@param ... any
function on_download(...) end
---@param ... any
function on_fetch(...) end

-- --------------------------------------------------------------------------
-- toolchain scope
-- --------------------------------------------------------------------------

---@param cross string
function set_cross(cross) end
---@param dir string
function set_bindir(dir) end
---@param dir string
function set_sdkdir(dir) end
---@param ... string
function set_archs(...) end
---@param ... string
function set_formats(...) end
---@param name string|table
function add_toolset(name) end

-- --------------------------------------------------------------------------
-- xmake library globals
-- --------------------------------------------------------------------------

---The `path` module of xmake (Lua itself has no such library).
---@class YaPathLib
---@field join fun(...: string): string
---@field directory fun(filepath: string): string
---@field basename fun(filepath: string): string
---@field filename fun(filepath: string): string
---@field extension fun(filepath: string): string
---@field absolute fun(filepath: string): string
---@field relative fun(filepath: string, rootdir?: string): string
---@field normalize fun(filepath: string): string
---@field is_absolute fun(filepath: string): boolean
---@field unix fun(filepath: string): string
---@field windows fun(filepath: string): string
---@field translate fun(filepath: string): string
path = {}

-- Modules this repo pulls in with the bare `import("...")` form, which binds the module to
-- a global named after it (`import("lib.detect.find_tool")` defines `find_tool`).

---Locate a program, e.g. `find_tool("python3")`; the result carries `name` and `program`.
---@param name string
---@param opt? table
---@return table|nil
function find_tool(name, opt) return nil end

---Locate a library, e.g. `find_library("vulkan")`; the result carries `links`, `linkdirs`, ...
---@param name string
---@param opt? table
---@return table|nil
function find_library(name, opt) return nil end

---@param opt? table
---@return table|nil
function find_vulkansdk(opt) return nil end

---The `core.project.depend` module, used to skip work that is already up to date.
---@class YaImportedDepend
---@field on_changed fun(callback: fun(...), opt?: table)
---@field is_changed fun(dependinfo: table, opt?: table): boolean
---@field load fun(dependfile: string, opt?: table): table
---@field save fun(dependinfo: table, dependfile: string)
depend = {}

---The `utils.progress` module used to draw the build progress bar.
---@class YaImportedProgress
---@field show fun(progress: number, format: string, ...: any)
---@field text fun(progress: number, format: string, ...: any)
---@field set_target fun(target: table)
---@field refresh fun()
progress = {}
