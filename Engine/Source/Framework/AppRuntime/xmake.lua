-- App runtime (compat alias). The real sources moved to GUI/Host
-- (ya-gui-host); this target keeps the legacy `AppRuntime/` include root alive
-- for one round so stale add_deps()/includes don't break. Delete in Phase A5
-- once no consumer references ya-app-runtime.
target("ya-app-runtime")
    set_kind("phony")
    add_includedirs("./include", { public = true })
    add_deps("ya-gui-host", { public = true })
