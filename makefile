# ============================================================================
# YA Engine - two-mode launcher (thin wrapper over xmake / Script/ya.py)
#
# Mode 1 - run a target directly (xmake):
#   make b TARGET=GUIWorkbench
#   make r TARGET=ya-gui-minimal-host ARGS="--exit-after-frame=30"
#   make r TARGET=ya-testing ARGS="--gtest_filter=Suite.Test"
#
# Mode 2 - engine / project / editor (Script/ya.py):
#   make run t=HelloMaterial
#   make run-editor t=HelloMaterial
#   make build t=HelloMaterial
#   make package t=HelloMaterial
#
# The build system stays XMake-only (AGENTS.md rule 1): every target just
# forwards to `xmake` / `Script/ya.py`.
# ============================================================================

t      := HelloMaterial
TARGET ?= ya-runtime
ARGS   ?=
YA     := uv run ./Script/ya.py

.PHONY: help b r run run-editor build package

help: ## list targets
	@echo "YA Engine launcher - two modes:"
	@echo ""
	@echo "Mode 1 - run a target directly (xmake):"
	@echo "  make b TARGET=GUIWorkbench"
	@echo "  make r TARGET=GUIWorkbench ARGS=\"--smoke-actions\""
	@echo "  make r TARGET=ya-gui-minimal-host ARGS=\"--exit-after-frame=30\""
	@echo ""
	@echo "Mode 2 - engine / project / editor (Script/ya.py):"
	@echo "  make run t=HelloMaterial"
	@echo "  make run-editor t=HelloMaterial"
	@echo "  make build t=HelloMaterial"
	@echo "  make package t=HelloMaterial"
	@echo ""
	@echo "Variables: TARGET (xmake target), t (game project), ARGS (extra args)"

# ---- Mode 1: direct target (xmake) ----

b: ## build a target directly: make b TARGET=GUIWorkbench
	xmake b $(TARGET)

r: ## run a target directly: make r TARGET=GUIWorkbench ARGS="--smoke-actions"
	xmake r $(TARGET) $(ARGS)

# ---- Mode 2: engine / project / editor (Script/ya.py) ----

run: ## run a game project through the engine: make run t=HelloMaterial
	$(YA) run --project ./Example/$(t)/$(t).yaproject -- $(ARGS)

run-editor: ## launch the editor for a project: make run-editor t=HelloMaterial
	$(YA) run-editor --project ./Example/$(t)/$(t).yaproject -- $(ARGS)

build: ## build a project closure: make build t=HelloMaterial
	$(YA) build --project ./Example/$(t)/$(t).yaproject

package: ## collect a minimal package: make package t=HelloMaterial
	$(YA) package --project ./Example/$(t)/$(t).yaproject
