# Progress

## C1 — done

Factory `DefaultEditorDockLayout.json` is the first-run / fallback document. `EditorDockWorkspace::applyWorkspaceLayout` reads `editor.dockLayout` when present, otherwise the factory JSON. `applyLayoutDocument` spawns known keys, sanitizes unknown keys (`FDockContext::sanitizeLayoutJson`), then `importLayoutJson`. Failed user docs fall back to factory after closing extras not in the factory key set.

Deleted `kDefaultWorkspaceTabs`, `applyDefaultLayout` C++ splits, `materializeWorkspaceTabs`, and `tryRestoreLayout`. Import stays strict (`ImportRejectsUnknownPanelKey` still passes).

Verify: `xmake b ya-game-editor`; `DockNodeTest.*` 27 passed; `EditorDockWorkspaceTest.*:EditorTabSpawnerRegistryTest.*` 4 passed.

## C2 — pending

Window menu, all tabs closable, last-focused-leaf invoke, drop Tools tab list.

## Not in this line

- Named user layouts
- Closed-tab geometry memory
- Workbench as a GameEditor dock tab
