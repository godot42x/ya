
t:=

.PHONY: package, cfg

cfg:
	xmake f -m debug
	xmake project -k compile_commands

r:
	uv run ./Script/ya.py run-editor --project  ./Example/$(t)/$(t).yaproject

rg:
	uv run ./Script/ya.py run --project  ./Example/$(t)/$(t).yaproject

ed:
	uv run ./Script/ya.py run-editor 

package:
	uv run  Script/ya.py package --project Example/$(t)/$(t).yaproject
	