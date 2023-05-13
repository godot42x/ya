
.PHONY: compile_commands

cfg:
	xmake m debug

compile_commands:
	xmake project -k compile_commands
