# ============================================================================
# YA Engine - game + GUI example launcher (thin wrapper)
#
# Build logic stays XMake-only (AGENTS.md rule 1): every target forwards to
# `xmake` / `Script/ya.py`; nothing is re-implemented here.
#
# Common variables:
#   t=HelloMaterial      game project name -> Example/<t>/<t>.yaproject
#   ARGS="..."           extra args appended to the run command
#
# Examples:
#   make run t=HelloMaterial
#   make run-editor t=GreedySnake
#   make run-gui
#   make run-gui ARGS="--run-arg=--exit-after-frame=30"
#   make run-workbench
#   make smoke-workbench
# ============================================================================

t      := HelloMaterial
ARGS   ?=
YA     := uv run ./Script/ya.py

.PHONY: help cfg run run-editor run-gui run-workbench smoke-workbench test-gui package profile lint shader rg ed

help: ## list available targets
	@echo "YA Engine launcher (thin wrapper over xmake / Script/ya.py)"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  make %-16s %s\n", $$1, $$2}'
	@echo ""
	@echo "Variables: t=<game project name>  ARGS=<extra run args>"

cfg: ## configure debug build + refresh compile_commands
	xmake f -m debug
	xmake project -k compile_commands

run: ## run a game project through ya-runtime (t=HelloMaterial|GreedySnake)
	$(YA) run --project ./Example/$(t)/$(t).yaproject -- $(ARGS)

run-editor: ## run the editor for a game project (t=...)
	$(YA) run-editor --project ./Example/$(t)/$(t).yaproject -- $(ARGS)

run-gui: ## run the standalone GUI smoke/example (long-running by default)
	$(YA) run-gui $(ARGS)

run-workbench: ## run the standalone GUI workbench (retain-mode tool app)
	$(YA) run-gui-workbench $(ARGS)

smoke-workbench: ## GUIWorkbench end-to-end automation (PASS/FAIL exit code)
	$(YA) run-gui-workbench --run-arg=--smoke-actions $(ARGS)

test-gui: ## GUI closure + widgets + workbench workspace unit tests
	xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
	xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test
	xmake b ya-gui-workbench-workspace-test && xmake r ya-gui-workbench-workspace-test

package: ## collect a minimal package for a game project (t=...)
	$(YA) package --project ./Example/$(t)/$(t).yaproject

profile: ## profile run (editor) + speedscope trace
	xmake f -m profile
	$(YA) run-editor --project ./Example/$(t)/$(t).yaproject
	npx speedscope "./Engine/Saved/Profile/profile-latest.speedscope.json"
	xmake f -m debug

lint: ## module boundary lint
	uv run ./Script/ya_module_lint.py

shader: ## regenerate shader headers
	xmake ya-shader

# Legacy aliases kept for muscle memory.
rg: run
ed: run-editor
