-- GUI app host (compat alias). The real sources moved to GUI/Host
-- (ya-gui-host); this target keeps the legacy `GUI/App/` include root alive
-- for one round so stale add_deps()/includes don't break. Delete in Phase A5
-- once no consumer references ya-gui-app-host.
target("ya-gui-app-host")
    set_kind("phony")
    add_includedirs("./include", { public = true })
    add_deps("ya-gui-host", { public = true })
